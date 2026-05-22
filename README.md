# MarinhoLab Gazebo images

`Gazebo` images used in MarinhoLab. Main use case is to serve as base images for
[`sas`](https://github.com/SmartArmStack/smart_arm_stack_ROS2) builds and templates.

## ROS Lyrical & Gazebo Jetty (Ubuntu 26.04)

```console
mkdir -p ~/marinholab/docker/gazebo/lyrical/
cd ~/marinholab/docker/gazebo/lyrical/
curl -OL https://raw.githubusercontent.com/MarinhoLab/gazebo/refs/heads/main/lyrical/compose.yml

xhost +local:root
docker compose up
```

## ROS Jazzy & Gazebo Harmonic (Ubuntu 24.04)

```console
mkdir -p ~/marinholab/docker/gazebo/jazzy/
cd ~/marinholab/docker/gazebo/jazzy/
curl -OL https://raw.githubusercontent.com/MarinhoLab/gazebo/refs/heads/main/jazzy/compose.yml

xhost +local:root
docker compose up
```