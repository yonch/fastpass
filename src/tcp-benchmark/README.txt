README.txt

Created on: January 8, 2014
Author: aousterh

This provides instructions for how to run tcp_sender and tcp_receiver
to measure various properties of a network.

You can run 3 types of experiments:
(0) interactive traffic with short-lived connections
(1) interactive traffic with persistent connections
(2) bulk traffic with persistent connections
You must specify the type of experiment you want as a command line
argument to both tcp_sender and tcp_receiver.

To run an experiment:
-Determine the max number of senders for each receiver. Set MAX_SENDERS
 in log.h based on this. Compile.
-Start a tcp_receiver on each machine which will receive traffic:
         tcp_receiver <receive_duration> <type> <port_num> > data.csv
 The receive duration is given in seconds. The type is 0, 1, or 2 depending
 on which of the experiment types you are running. The port_num is optional
 and defaults to 1100. It must agree with the sender port number.
-Start a tcp_sender on each machine which will send traffic, one per
 machine you are sending to:
 	 tcp_sender <send_duration> <mean_t> <id> <dest_ip> <size> <type> <port_num>
 The send duration is given in seconds and mean_t is the mean time between
 flows generated, given in microseconds (these two parameters are only used
 in experiment types 0 and 1 and are ignored otherwise). The id is the id of
 this sender (for ease of logging). If MAX_SENDERS==1, the id should be 0.
 With MAX_SENDERS==2, the ids used for the two tcp_senders should be 0 and 1,
 and so on. Dest_ip is the IP address of the destination machine in dotted
 notation (ex: 10.0.2.15). size parameterizes the amount of data sent. For
 experiment types 0 and 1, the size of each flow is uniformly distributed
 between 1 and size MTUs. For experiment type 2, size gives the amount of
 data to be sent, in MBs. Type is the experiment type (0, 1, or 2). The port
 number again is optional and defaults to 1100.
      
Example
For example, to run a bulk experiment sending 1 GB of data to a receiver at
10.0.2.15 (which listens for 10 seconds), I could run the following at the
receiver and sender, respectively:
./tcp_receiver 10 2 > data.csv
./tcp_sender 0 0 0 10.0.2.15 1000 2
