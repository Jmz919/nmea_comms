
#include "rx.h"
#include "checksum.h"

#include <poll.h>
#include <sstream>
#include <boost/thread.hpp>
#include <boost/algorithm/string.hpp>

#include "ros/ros.h"
#include "nmea_msgs/Sentence.h"


static void _handle_sentence(ros::Publisher& publisher, ros::Time& stamp, char* sentence)
{
  char* sentence_body = strtok(sentence, "*");
  char* sentence_checksum = strtok(NULL, "*");
  if (sentence_checksum == NULL) {
    ROS_DEBUG("No checksum marker (*), discarding sentence.");
    return;   
  }
  if (strlen(sentence_checksum) != 2) {
    ROS_DEBUG("Checksum wrong length, discarding sentence.");
    return;
  }

  char computed_checksum[2];
  compute_checksum(sentence, computed_checksum);
  if (memcmp(computed_checksum, sentence_checksum, 2) != 0) {
    ROS_DEBUG("Bad checksum, discarding sentence.");
    return;
  }

  nmea_msgs::Sentence sentence_msg;
  boost::split(sentence_msg.fields, sentence_body, boost::is_any_of(","));

  sentence_msg.talker = sentence_msg.fields[0].substr(0, 2);
  sentence_msg.sentence = sentence_msg.fields[0].substr(2);
  sentence_msg.fields.erase(sentence_msg.fields.begin());

  sentence_msg.header.stamp = stamp;
  publisher.publish(sentence_msg);
}

 
static void _thread_func(ros::NodeHandle& n, int fd)
{
  ros::Publisher pub = n.advertise<nmea_msgs::Sentence>("rx", 5);
  struct pollfd pollfds[] = { { fd, POLLIN, 0 } };
  char buffer[2048];
  char* buffer_write = buffer;
  char* buffer_end = &buffer[sizeof(buffer)];

  while(ros::ok()) {
    int retval = poll(pollfds, 1, 1000);

    if (retval == 0) {
      // No event, just 1 sec timeout.
      continue;
    } else if (retval < 0) {
      ROS_FATAL("Error polling device. Terminating node.");
      ros::shutdown();
    } else if (pollfds[0].revents & (POLLHUP | POLLERR)) {
      ROS_FATAL("Device error/hangup. Terminating node.");
      ros::shutdown(); 
    }

    // Read in contents of buffer and null-terminate it.
    ros::Time now = ros::Time::now();
    retval = read(fd, buffer_write, buffer_end - buffer_write - 1);
    if (retval > 0) {
      buffer_write += retval;
    } else {
      ROS_WARN("Error reading from device.");
      ros::Duration(1.0).sleep();  // just in case.
    }
    ROS_DEBUG_STREAM("Buffer size after reading from fd: " << buffer_write - buffer);
    *buffer_write = '\0';

    char* buffer_read = buffer;
    while(1) {
      char* sentence = strchr(buffer_read, '$');
      if (sentence == NULL) break;
      char* sentence_end = strchr(sentence, '\r');
      if (sentence_end == NULL) break;
      *sentence_end = '\0';
      _handle_sentence(pub, now, sentence + 1);
      buffer_read = sentence_end + 1; 
    }

    int remainder = buffer_write - buffer_read;
    ROS_DEBUG_STREAM("Remainder in buffer is: " << remainder);
    memcpy(buffer, buffer_read, remainder);
    buffer_write = buffer + remainder;
  }
}


static boost::thread* rx_thread_ptr;

void rx_start(ros::NodeHandle& n, int fd)
{
  rx_thread_ptr = new boost::thread(_thread_func, boost::ref(n), fd);
}

void rx_stop()
{
  rx_thread_ptr->interrupt();
  delete rx_thread_ptr;
}
