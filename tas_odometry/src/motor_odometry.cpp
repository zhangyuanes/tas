/* Motor Odometry
 *
 * This node reads in encoder data from an Arduino monitoring a sensored
 *  brushless motor. It append the length driven (according to the motor
 *  encoders) to the X dimension of this pose.
 *
 * ROS parameters:
 * ticks_per_meter - calibrated number of encoder ticks per meter (default 310)
 * frame_id - frame_id of header of Twist message (default: base_link)
 * uncertainty_fixed - uncertainty in Covariance matrix of Twist (default 1e-3)
 * deadline_timeout - after this amount of time (s), 0 vel will be republished
 *  (default: 0.1)
 *
 * Laurenz 2015-01
 * ga68gug / TUM LSR TAS
 */

#include <ros/ros.h>
#include <geometry_msgs/TwistWithCovarianceStamped.h>
#include <std_msgs/Int32.h>
#include <tas_odometry/Encoder.h>
#include <boost/asio.hpp>
#include <boost/thread.hpp>

ros::Publisher pose_publisher;
ros::Publisher encoder_publisher;

geometry_msgs::TwistWithCovarianceStamped twist;
int32_t encoder_abs = 0;

std::string frame_id;
double ticks_per_meter;
double uncertainty_fixed;
double deadline_timeout;

// timestamp (sec) of the last time a twist message was published
double last_publish_time = 0;


// We get new encoder values here
void encoder_callback(const tas_odometry::Encoder::ConstPtr& encoder_data) {

	// Update twist msg header
	twist.header.stamp = ros::Time::now();
	twist.header.seq++;

	// Convert and save time of message arrival
	double time_now = twist.header.stamp.toSec();
	last_publish_time = time_now;

	// Publish integrated/absolute encoder value
	encoder_abs += encoder_data->encoder_ticks;
	std_msgs::Int32 msg;
	msg.data = encoder_abs;
	encoder_publisher.publish(msg);

	// Convert length to meter
	double change_meter = (double) encoder_data->encoder_ticks / ticks_per_meter;
	// Convert time to seconds
	double change_time = (double) encoder_data->duration / 1e6;
	// Calc velocity
	double vel = change_meter / change_time;

	// Add it to message
	twist.twist.twist.linear.x = vel;

	// Send out message
	pose_publisher.publish(twist);

}

int main(int argc, char** argv) {
	ros::init(argc, argv, "motor_odometry");
	ros::NodeHandle n("~");

	// ROS params
	n.param<double>("ticks_per_meter", ticks_per_meter, 310);
	n.param<std::string>("frame_id", frame_id, "base_link");
	n.param<double>("uncertainty_fixed", uncertainty_fixed, 1e-3);
	n.param<double>("deadline_timeout", deadline_timeout, 0.1);

	// Setup twist message
	twist.header.seq = 0;
	twist.header.frame_id = frame_id;

	// Fill covariance. Order: (x, y, z, rotation about X axis, rotation about Y axis, rotation about Z axis)
	twist.twist.covariance.assign(0.0); // Generally uncorrelated
	for (int i=0; i<36; i+=7)
		twist.twist.covariance.elems[i] = 999;
	twist.twist.covariance.elems[0] = uncertainty_fixed; // x

	// ROS subs, pubs
	ros::Subscriber encoder_sub = n.subscribe("/motor_encoder", 100, encoder_callback);
	pose_publisher = n.advertise<geometry_msgs::TwistWithCovarianceStamped>("motor_odom", 50);
	encoder_publisher = n.advertise<std_msgs::Int32>("motor_encoder_abs", 50);


	// Spin until node is shut down
	while (ros::ok()) {

		// Handle ROS
		ros::spinOnce();

		// After some time, send out a 0-vel message to make clear to the EKF that nothing happened
		double time_now = ros::Time::now().toSec();
		if(time_now - last_publish_time > deadline_timeout) {
			last_publish_time = time_now;
			twist.header.stamp = ros::Time::now();
			twist.header.seq++;
			twist.twist.twist.linear.x = 0.0;
			pose_publisher.publish(twist);
		}

		// No rate-limiting, since this is (more or less) event-based
	}

	return 0;
}
