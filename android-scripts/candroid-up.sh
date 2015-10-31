#!/system/bin/sh

# don't need these for nexus 9
# load_usb_driver(){
# 	insmod /data/local/tmp/modules/usb_8dev.ko
# }
# 
# load_isobus(){
# 	insmod /data/local/tmp/modules/can-isobus.ko
# }

setup_can(){
	ip link set can0 type can bitrate 250000
	# ip link set can1 type can bitrate 250000
	ip link set up can0
	# ip link set up can1
}

start_logger(){
	./data/local/can/can_log_raw
}

# load_usb_driver
# echo "usb_8dev loaded"
# load_isobus
# echo "can-isobus loaded"
setup_can
echo "can is up"
start_logger
echo "logger started"
