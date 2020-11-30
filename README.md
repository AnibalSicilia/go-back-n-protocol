# go-back-n-protocol
Implementation of sender/receiver to communicate through GBN protocol using UDP.


Note: Checksum was implemented as a variation of Kurose's prog2.c which was consulted on this website: www.cise.ufl.edu/~mgargano/part2/prog2.c
Even though the 8-bit-checksum implemented in homework was interesting in the use of char elements and this approach of converting everything
to integer format seems OK to me as well, it is not the same as his. I think mine is an improved version.

Also, because of the incredible speed of the time out (10 milisecondes) it is recommended to place twister interval with a value between
0-1 miliseconds. Below it is an adequate set up to run programs.

Compile using make.

Start either twister or receiver, then as the last one run the sender.

IMPORTANT: Sometimes the sender will fail to receive last acknowledgement from receiver even though the file have been received successfully.
In this case the throughput will not be printed out because the transfer has been considered a mistake. Just run again the program and the 
throughput will be printed successfully next time.

---------------------------------------------------------------------------------------------------------------------------------------------
sender:
usage: %s <sender-port> <receiver-ip> <receiver-port> <filename> <max-windows-size>\n", argv[0]);

typical sample used: ./sender 6787 localhost 6788 andromeda.jpg 100
---------------------------------------------------------------------------------------------------------------------------------------------
receiver:
Usage: %s <port-number> <filename>

typical sample used: ./receiver 6789 picture.jpg
---------------------------------------------------------------------------------------------------------------------------------------------
twister:
usage: %s <twister_rcv_port> <sender_addr> <sender_port> <receiver_addr> <receiver_port> <prob_corrupt> <prob_drop> <prob_reorder> <interval>

typical sample used ./twister 6788 localhost 6787 localhost 6789 0.05 0.01 0.0 0
---------------------------------------------------------------------------------------------------------------------------------------------

Note: I tested this program with files in the range of 300 KB - 4.0 MB. Something below this range will be no problem. Did not test above 
4.0 MB barrier.

- All may tests were performed in the scope of 100 window-size. Much better results if window-size smaller (done when under construction).

- I also tested the software with a file of up to 4.0 MB (eiffel.jpg) and the program performs smoothly. Be careful of your computer because
the twister could be really dangerous.
