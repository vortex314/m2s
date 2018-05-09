# mqtt to serial command line
this tool works together with serial2mqtt to read and display the serial output, but also to send a binary file to be flashed.

Different modes of operation :
- manual, waits for a key press (P) to send next version of binary file to dst/DEVICE-SERIAL/serial2mqtt/flash 

- all data received on src/DEVICE-SERIAL/serial2mqtt/log is just displayed on stdout.
- all mqtt published by device can also be monitored by supplying -m options. The logical device is found on src/DEVICE-SERIAL/serial2mqtt/device where the logical DEVICE will appear. 
> Written with [StackEdit](https://stackedit.io/).
