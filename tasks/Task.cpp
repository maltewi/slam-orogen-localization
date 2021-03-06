/* Generated from orogen/lib/orogen/templates/tasks/Task.cpp */

#include "Task.hpp"
#include <base/samples/Pointcloud.hpp>

using namespace localization;

Task::Task(std::string const& name)
    : TaskBase(name), bodyName("body"), worldName("world")
{
}

Task::Task(std::string const& name, RTT::ExecutionEngine* engine)
    : TaskBase(name, engine), bodyName("body"), worldName("world")
{
}

Task::~Task()
{
}

void MLSEventHandler::handle( const envire::Event& event )
{
    envire::MultiLevelSurfaceGrid* mls_grid = dynamic_cast<envire::MultiLevelSurfaceGrid*>(event.a.get());
    if(event.type == envire::event::ITEM && 
       (event.operation == envire::event::ADD || event.operation == envire::event::UPDATE) && 
        mls_grid)
    {
        task->gotNewMls = true;
    }
}

void Task::createPointcloudFromMLS(PCLPointCloudPtr pointcloud, envire::MultiLevelSurfaceGrid* mls_grid)
{
    pointcloud->clear();
    
    float vertical_distance = (mls_grid->getScaleX() + mls_grid->getScaleY()) * 0.5;
    if(vertical_distance <= 0.0)
        vertical_distance = 0.1;
    
    // create pointcloud from mls
    for(size_t x=0;x<mls_grid->getCellSizeX();x++)
    {
        for(size_t y=0;y<mls_grid->getCellSizeY();y++)
        {
            for( envire::MLSGrid::iterator cit = mls_grid->beginCell(x,y); cit != mls_grid->endCell(); cit++ )
            {
                envire::MLSGrid::SurfacePatch p( *cit );
                
                Eigen::Vector3d cellPosWorld = mls_grid->fromGrid(x, y, mls_grid->getEnvironment()->getRootNode());
                pcl::PointXYZ point;
                point.x = cellPosWorld.x();
                point.y = cellPosWorld.y();
                point.z = cellPosWorld.z();
                if(p.isHorizontal())
                {
                    point.z = cellPosWorld.z() + p.mean;
                    pointcloud->push_back(point);
                }
                else if(p.isVertical())
                {
                    float min_z = (float)p.getMinZ(0);
                    float max_z = (float)p.getMaxZ(0);
                    for(float z = min_z; z <= max_z; z += vertical_distance)
                    {
                        point.z = cellPosWorld.z() + z;
                        pointcloud->push_back(point);
                    }
                }
            }
        }
    }
}

void Task::updateICPModelFromMap(envire::MultiLevelSurfaceGrid* mls_grid)
{
    map_pointcloud->clear();
    model_cloud.points.clear();
    model_cloud.colors.clear();
    Eigen::Affine3f map2world_affine(map2world.getTransform());
    
    createPointcloudFromMLS(map_pointcloud, mls_grid);
    
    for(unsigned i = 0; i < map_pointcloud->size(); i++)
    {
	map_pointcloud->at(i).getVector3fMap() = map2world_affine * map_pointcloud->at(i).getVector3fMap();
	model_cloud.points.push_back(map_pointcloud->at(i).getVector3fMap().cast<double>());
	model_cloud.colors.push_back(base::Vector4d(0.0, 1.0, 0.0, 1.0));
    }
    
    _debug_map_pointcloud.write(model_cloud);
    
    if(map_pointcloud->size())
    {
	PCLPointCloudPtr target_pointcloud(new PCLPointCloud());
	target_pointcloud->points = map_pointcloud->points;
        icp->setInputTarget(target_pointcloud);
    }

}

void Task::alignPointcloudAsMLS(const base::Time& ts, const std::vector< base::Vector3d >& sample_pointcloud, const envire::TransformWithUncertainty& body2odometry)
{
    if(sample_pointcloud.size() == 0)
	return;
    boost::shared_ptr<envire::Environment> pointcloud_env;
    pointcloud_env.reset(new envire::Environment());
    envire::MLSProjection* pointcloud_projection = new envire::MLSProjection();
    pointcloud_projection->useNegativeInformation(false);
    pointcloud_projection->useUncertainty(true);
    double grid_size = 200.0;
    double cell_resolution = 0.1;
    double grid_count = grid_size / cell_resolution;
    envire::MultiLevelSurfaceGrid* pointcloud_grid = new envire::MultiLevelSurfaceGrid(grid_count, grid_count, cell_resolution, cell_resolution, -0.5 * grid_size, -0.5 * grid_size);
    pointcloud_grid->getConfig().updateModel = envire::MLSConfiguration::KALMAN;
    pointcloud_grid->getConfig().gapSize = 0.5;
    pointcloud_grid->getConfig().thickness = 0.05;
    pointcloud_env->attachItem(pointcloud_grid, pointcloud_env->getRootNode());
    pointcloud_env->addOutput(pointcloud_projection, pointcloud_grid);
    
    // create envire pointcloud
    envire::Pointcloud* pc = new envire::Pointcloud();
    pc->vertices.reserve(sample_pointcloud.size());
    for(unsigned i = 0; i < sample_pointcloud.size(); i++)
	pc->vertices.push_back(sample_pointcloud[i]);
    
    // update mls
    envire::MLSGrid* grid = pointcloud_projection->getOutput<envire::MLSGrid*>();
    if(!grid)
    {
	RTT::log(RTT::Error) << "Missing mls grid in pointcloud environment." << RTT::endlog();
	return;
    }
    grid->clear();
    pointcloud_env->attachItem(pc, pointcloud_env->getRootNode());
    pointcloud_env->addInput(pointcloud_projection, pc);
    pointcloud_projection->updateAll();
        
    // remove inputs
    pointcloud_env->removeInput(pointcloud_projection, pc);
    pointcloud_env->detachItem(pc, true);

    // create pointcloud from mls_grid
    PCLPointCloudPtr pcl_pointcloud(new PCLPointCloud());
    createPointcloudFromMLS(pcl_pointcloud, grid);
    
    // create debug pointcloud
    aligned_cloud.points.clear();
    aligned_cloud.colors.clear();
    aligned_cloud.points.reserve(pcl_pointcloud->size());
    for(unsigned i = 0; i < pcl_pointcloud->size(); i++)
    {
	Eigen::Vector3f point = pcl_pointcloud->at(i).getVector3fMap();
    	aligned_cloud.points.push_back(base::Vector3d(point.x(), point.y(), point.z()));
    }
    aligned_cloud.colors.resize(aligned_cloud.points.size(), base::Vector4d(1.0, 0.0, 0.0, 1.0));
    
    alignPointcloud(ts, pcl_pointcloud, body2odometry);
}

void Task::alignPointcloud(const base::Time &ts, const std::vector<base::Vector3d>& sample_pointcloud, const envire::TransformWithUncertainty& body2odometry)
{
    if(_convert_pc_to_mls.get())
	alignPointcloudAsMLS(ts, sample_pointcloud, body2odometry);
    else
    {
	aligned_cloud.points = sample_pointcloud;
	aligned_cloud.colors.resize(sample_pointcloud.size(), base::Vector4d(1.0, 0.0, 0.0, 1.0));
	PCLPointCloudPtr pcl_pointcloud(new PCLPointCloud());
	pcl_pointcloud->reserve(std::max((u_int64_t)sample_pointcloud.size(), (u_int64_t)gicp_config.max_input_sample_count));
	std::vector<bool> mask;
	computeSampleMask(mask, sample_pointcloud.size(), gicp_config.max_input_sample_count);
	pcl::PointXYZ point;
	for(unsigned i = 0; i < sample_pointcloud.size(); i++)
	{
	    if(mask[i])
	    {
		point.getVector3fMap() = sample_pointcloud[i].cast<float>();
		pcl_pointcloud->push_back(point);
	    }
	}
	alignPointcloud(ts, pcl_pointcloud, body2odometry);
    }
}

void Task::alignPointcloud(const base::Time &ts, const std::vector<Eigen::Vector3d>& sample_pointcloud, const envire::TransformWithUncertainty& body2odometry)
{
    aligned_cloud.points.clear();
    aligned_cloud.colors.resize(sample_pointcloud.size(), base::Vector4d(1.0, 0.0, 0.0, 1.0));
    PCLPointCloudPtr pcl_pointcloud(new PCLPointCloud());
    pcl_pointcloud->reserve(std::max((u_int64_t)sample_pointcloud.size(), (u_int64_t)gicp_config.max_input_sample_count));
    std::vector<bool> mask;
    computeSampleMask(mask, sample_pointcloud.size(), gicp_config.max_input_sample_count);
    pcl::PointXYZ point;
    for(unsigned i = 0; i < sample_pointcloud.size(); i++)
    {
        if(mask[i])
        {
            point.getVector3fMap() = sample_pointcloud[i].cast<float>();
            pcl_pointcloud->push_back(point);
        }
        aligned_cloud.points.push_back(sample_pointcloud[i]);
    }
    alignPointcloud(ts, pcl_pointcloud, body2odometry);
}

void Task::alignPointcloud(const base::Time& ts, const PCLPointCloudPtr sample_pointcoud, const envire::TransformWithUncertainty& body2odometry)
{
    if(sample_pointcoud->empty())
    {
        std::cout << "Input cloud is empty" << std::endl;
        return;
    }
    if(!icp->getInputTarget().get())
    {
        std::cout << "No Input Target" << std::endl;
        return;
    }
    Eigen::Affine3d odometry_delta = last_odometry2body.getTransform() * body2odometry.getTransform();
    Eigen::Affine3d transformation_guess = last_body2world.getTransform() * odometry_delta;
    last_icp_match = base::Time::now();

    icp->setInputSource(sample_pointcoud);
    
    /** stupid way to fix a pcl bug */
    for(unsigned i = 0; i < icp->target_->size(); i++)
    {
	pcl::PointXYZ& point = const_cast<pcl::PointXYZ&>(icp->target_->at(i));
	point.getVector3fMap() = transformation_guess.inverse().cast<float>() * map_pointcloud->at(i).getVector3fMap();
    }
    icp->tree_->setInputCloud(icp->target_);
    /** ** */

    std::cout << "Doing ICP match " << std::endl;

    base::Time start = base::Time::now();
    // Perform the alignment
    PCLPointCloud cloud_source_registered;
    icp->align(cloud_source_registered);//, transformation_guess.matrix().cast<float>());
    double fitness_score = icp->getFitnessScore();
    if(icp->hasConverged() && fitness_score <= gicp_config.max_mean_square_error)
    {
	new_state = RUNNING;
	RTT::log(RTT::Error) << "ICP alignment successful. FitnessScore: " << fitness_score<< RTT::endlog();
	
        Eigen::Affine3d transformation(icp->getFinalTransformation().cast<double>());
        
        last_body2world.setTransform(transformation_guess * transformation);

        last_body2odometry = body2odometry;
        last_odometry2body = body2odometry.inverse();
        
        std::cout << "Got new ICP match " << last_body2world.getTransform().translation().transpose() << std::endl;
	
	icp_debug.successful_alignments++;
        
        //write out current odometry sample
        updatePosition(ts, last_body2odometry.getTransform(), true);
    }
    else
    {
        std::cout << "ICP failed " << std::endl;
        RTT::log(RTT::Error) << "ICP alignment failed, perhaps a model update is necessary. FitnessScore: " << fitness_score << RTT::endlog();
        new_state = ICP_ALIGNMENT_FAILED;
	icp_debug.failed_alignments++;
    }
    base::Time end = base::Time::now();
    
    icp_debug.time = ts;
    icp_debug.last_fitness_score = fitness_score;
    _icp_debug_information.write(icp_debug);

    std::cout << "icp took " << end-start << std::endl;
    
    if(_write_debug_pointcloud)
    {
	for(unsigned i = 0; i < aligned_cloud.points.size(); i++)
	{
	    aligned_cloud.points[i] = last_body2world.getTransform() * aligned_cloud.points[i];
	}
	base::samples::Pointcloud debug_cloud;
	debug_cloud.points = model_cloud.points;
	debug_cloud.colors = model_cloud.colors;
	debug_cloud.points.insert(debug_cloud.points.end(), aligned_cloud.points.begin(), aligned_cloud.points.end());
	debug_cloud.colors.insert(debug_cloud.colors.end(), aligned_cloud.colors.begin(), aligned_cloud.colors.end());
	_debug_map_pointcloud.write(debug_cloud);
    }
}

void Task::updatePosition(const base::Time &curTime, const Eigen::Affine3d &curBody2Odometry, bool write)
{
    if(!write)
        return;
    
    //comput delta between odometry position at the time of the last ICP match and the current odometry position
    Eigen::Affine3d curBody2BodyICP = last_odometry2body.getTransform() * curBody2Odometry;
//     Eigen::Affine3d odometry_delta = last_odometry2body.getTransform() * curBody2Odometry;
    base::samples::RigidBodyState sample_out;
    sample_out.invalidate();
    sample_out.time = curTime;
    sample_out.setTransform(last_body2world.getTransform() * curBody2BodyICP);
    sample_out.sourceFrame = bodyName;
    sample_out.targetFrame = worldName;
    _pose_samples.write(sample_out);
    graph_slam::PoseProviderUpdate pose_provider_update;
    pose_provider_update.body2world = sample_out.getPose();
    pose_provider_update.body2odometry = base::Pose(curBody2Odometry);
    _pose_provider_update.write(pose_provider_update);
}

void Task::computeSampleMask(std::vector<bool>& mask, unsigned pointcloud_size, unsigned samples_count)
{
    mask.clear();
    if(samples_count == 0)
        return;
    if(samples_count >= pointcloud_size)
    {
        mask.resize(pointcloud_size, true);
        return;
    }
    
    mask.resize(pointcloud_size, false);
    unsigned samples_drawn = 0;
    
    while(samples_drawn < samples_count)
    {
        unsigned index = rand() % pointcloud_size;
        if(mask[index] == false)
        {
            mask[index] = true;
            samples_drawn++;
        }
    }
}

bool Task::newICPRunPossible(const Eigen::Affine3d& body2odometry) const
{
    if((last_body2odometry.getTransform().inverse() * body2odometry).translation().norm() > gicp_config.icp_match_interval || (base::Time::now() - last_icp_match).toSeconds() > gicp_config.icp_match_interval_time)
	return true;
    return false;
}


/// The following lines are template definitions for the various state machine
// hooks defined by Orocos::RTT. See Task.hpp for more detailed
// documentation about them.

bool Task::configureHook()
{
    if (! TaskBase::configureHook())
        return false;
    
    last_state = PRE_OPERATIONAL;
    new_state = RUNNING;

    // set inital transformations
    last_body2world.setTransform(_start_pose.get().getTransform());
    last_body2odometry = envire::TransformWithUncertainty::Identity();
    last_odometry2body = envire::TransformWithUncertainty::Identity();
    map2world = envire::TransformWithUncertainty::Identity();
    init_odometry = true;
    
    last_icp_match.microseconds = 0;
    
    // reset map point cloud
    map_pointcloud.reset(new PCLPointCloud());

    // setup environment
    env.reset(new envire::Environment());
    env->addEventHandler(new MLSEventHandler(this));

    // set icp config
    gicp_config = _gicp_configuration.get();
    icp.reset(new pcl::GeneralizedIterativeClosestPoint<pcl::PointXYZ, pcl::PointXYZ>());
    icp->setMaxCorrespondenceDistance(gicp_config.max_correspondence_distance);
    icp->setMaximumIterations(gicp_config.maximum_iterations);
    icp->setTransformationEpsilon(gicp_config.transformation_epsilon);
    icp->setEuclideanFitnessEpsilon(gicp_config.euclidean_fitness_epsilon);
    icp->setCorrespondenceRandomness(gicp_config.correspondence_randomness);
    icp->setMaximumOptimizerIterations(gicp_config.maximum_optimizer_iterations);
    icp->setRotationEpsilon(gicp_config.rotation_epsilon);
    
    // load inital environment
    if(!_environment_path.get().empty())
    {
        boost::shared_ptr<envire::Environment> inital_env(envire::Environment::unserialize(_environment_path));
	try
	{
	    if(!inital_env.get())
		throw std::runtime_error("couldn't load inital environment.");
	    boost::intrusive_ptr<envire::MLSGrid> mls_grid = inital_env->getItem<envire::MLSGrid>();
            if(!mls_grid)
                throw std::runtime_error("Initial environment did not contain a mls");
	    updateICPModelFromMap(mls_grid.get());
	    RTT::log(RTT::Info) << "Successfully loaded inital multi-level surface grid." << RTT::endlog();
	}
	catch(std::runtime_error e)
	{
	    RTT::log(RTT::Error) << "Couldn't load inital multi-level surface grid: " << e.what() << RTT::endlog();
	}
    }
    gotNewMls = false;
    worldName = _outputFrameName.get();

    return true;
}
bool Task::startHook()
{
    if (! TaskBase::startHook())
        return false;
    return true;
}


void Task::updateHook()
{
    // read envire events
    envire::OrocosEmitter::Ptr binary_event;
    while(_envire_map.read(binary_event) == RTT::NewData)
    {
        env->applyEvents(*binary_event);
    }
    
    if(gotNewMls)
    {
        boost::intrusive_ptr<envire::MLSGrid> mls_grid = env->getItem<envire::MLSGrid>();
        updateICPModelFromMap(mls_grid.get());
        gotNewMls = false;
    }
    
    TaskBase::updateHook();

    // write state if it has changed
    if(last_state != new_state)
    {
        last_state = new_state;
        state(new_state);
    }
}
void Task::errorHook()
{
    TaskBase::errorHook();
}
void Task::stopHook()
{
    TaskBase::stopHook();
}
void Task::cleanupHook()
{
    TaskBase::cleanupHook();
}
