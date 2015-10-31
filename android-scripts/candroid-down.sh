#!/system/bin/sh

# don't need it for nexus 9
# unload_usb_driver(){
# 	rmmod usb_8dev
# }
#
# unload_isobus(){
# 	rmmod can-isobus
# }

down_can(){
	ip link set down can0
	# ip link set down can1
}

kill_logger(){
	set `ps | grep can_log_raw`
	PID=$(echo $2)
	kill -9 $PID
}

kill_logger
echo "logger killed"
down_can
echo "can is down"
# unload_usb_driver
# echo "usb_8dev unloaded"
# unload_isobus
# echo "can-isobus unloaded"
