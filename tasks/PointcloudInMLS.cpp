/* Generated from orogen/lib/orogen/templates/tasks/Task.cpp */

#include "PointcloudInMLS.hpp"

using namespace localization;



PointcloudInMLS::PointcloudInMLS(std::string const& name)
    : PointcloudInMLSBase(name)
{
}

PointcloudInMLS::PointcloudInMLS(std::string const& name, RTT::ExecutionEngine* engine)
    : PointcloudInMLSBase(name, engine)
{
}

PointcloudInMLS::~PointcloudInMLS()
{
}

void PointcloudInMLS::odometryCallback(base::Time ts)
{
    Eigen::Affine3d body2Odometry;
    if(!_body2odometry.get(ts, body2Odometry, false))
        return;

    updatePosition(ts, body2Odometry);
}


void PointcloudInMLS::pointcloud_samplesTransformerCallback(const base::Time &ts, const ::base::samples::Pointcloud &pointcloud_samples_sample)
{
    Eigen::Affine3d pointcloud2body;
    if (!_pointcloud2body.get(ts, pointcloud2body))
    {
        RTT::log(RTT::Error) << "skip, have no pointcloud2odometry transformation sample!" << RTT::endlog();
        new_state = TaskBase::MISSING_TRANSFORMATION;
        return;
    }
    envire::TransformWithUncertainty body2odometry;
    if (!_body2odometry.get(ts, body2odometry, true))
    {
        RTT::log(RTT::Error) << "skip, have no body2odometry transformation sample!" << RTT::endlog();
        new_state = TaskBase::MISSING_TRANSFORMATION;
        return;
    }
    
    
    if(newICPRunPossible(body2odometry.getTransform()))
    {
	updatePosition(ts, body2odometry.getTransform(), true);
	
	if(pointcloud2body.matrix() != Eigen::Matrix4d::Identity())
	{
	    // apply transformation
	    std::vector<base::Vector3d> transformed_pointcloud;
	    transformed_pointcloud.reserve(pointcloud_samples_sample.points.size());
	    for(std::vector<base::Vector3d>::const_iterator it = pointcloud_samples_sample.points.begin(); it != pointcloud_samples_sample.points.end(); it++)
	    {
		transformed_pointcloud.push_back(pointcloud2body * (*it));
	    }
	    alignPointcloud(ts, transformed_pointcloud, body2odometry);
	}
	else
	    alignPointcloud(ts, pointcloud_samples_sample.points, body2odometry);
    }
}


/// The following lines are template definitions for the various state machine
// hooks defined by Orocos::RTT. See PointcloudInMLS.hpp for more detailed
// documentation about them.

bool PointcloudInMLS::configureHook()
{
    if (! PointcloudInMLSBase::configureHook())
        return false;
    
    bodyName = _body_frame.get();
    _body2odometry.registerUpdateCallback(boost::bind(&PointcloudInMLS::odometryCallback, this, _1));
    
    return true;
}
bool PointcloudInMLS::startHook()
{
    if (! PointcloudInMLSBase::startHook())
        return false;
    return true;
}
void PointcloudInMLS::updateHook()
{
    PointcloudInMLSBase::updateHook();
}
void PointcloudInMLS::errorHook()
{
    PointcloudInMLSBase::errorHook();
}
void PointcloudInMLS::stopHook()
{
    PointcloudInMLSBase::stopHook();
}
void PointcloudInMLS::cleanupHook()
{
    PointcloudInMLSBase::cleanupHook();
}
