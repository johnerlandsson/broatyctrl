CFLAG = -Wall
CC = gcc
INCLUDE = -I/home/pi/src/SOEM/soem/ -I/home/pi/src/SOEM/osal -I/home/pi/src/SOEM/osal/linux -I/home/pi/src/SOEM/oshw/linux
LIBS = -L/home/pi/src/SOEM/build -lsoem -lpthread

all: broatyctrl

broatyctrl: broatyctrl.o
	${CC} -o broatyctrl broatyctrl.o ${LIBS}


broatyctrl.o: broatyctrl.c
	${CC} -c ${CFLAG} broatyctrl.c ${INCLUDE}

clean: 
	rm broatyctrl.o broatyctrl
