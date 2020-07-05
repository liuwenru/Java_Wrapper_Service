#!/bin/bash


docker run -it --net=host --privileged=true --name=javawrapperservice -v /root/tools/apps:/opt/apps   -v $(pwd):/root/Java_Wrapper_Service -e sshport=2225 -e rootpass=liuwenru liuwenru/centos_dev


