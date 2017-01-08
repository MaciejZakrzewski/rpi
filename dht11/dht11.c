#include <wiringPi.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <string.h>
#define MAX_TIME 85
#define MAX_TRIES 100
#define DHT11PIN 7

// ---------------------------------------------------
// DHT11 FUNCTIONS
// ---------------------------------------------------
int dht11_val[5]={0,0,0,0,0};

int dht11_read_val(int *h, int *t) {
  uint8_t lststate=HIGH;
  uint8_t counter=0;
  uint8_t j=0,i;
  
  for (i=0;i<5;i++) {
     dht11_val[i]=0;
  }

  pinMode(DHT11PIN,OUTPUT);
  digitalWrite(DHT11PIN,LOW);
  delay(18);
  digitalWrite(DHT11PIN,HIGH);
  delayMicroseconds(40);
  pinMode(DHT11PIN,INPUT);

  for (i=0; i < MAX_TIME; i++) {
    counter=0;
    while (digitalRead(DHT11PIN) == lststate){
      counter++;
      delayMicroseconds(1);
      if (counter == 255) {
        break;
      }
    }
    lststate=digitalRead(DHT11PIN);
    if (counter == 255) {
       break;
    }
    // top 3 transitions are ignored
    if ((i >= 4) && (i%2 == 0)) {
      dht11_val[j/8]<<=1;
      if(counter>16) {
        dht11_val[j/8]|=1;
      }
      j++;
    }
  }

  // verify cheksum and print the verified data
  if ((j>=40) && (dht11_val[4]==((dht11_val[0]+dht11_val[1]+dht11_val[2]+dht11_val[3])& 0xFF))) {
    // Only return the integer part of humidity and temperature. The sensor
    // is not accurate enough for decimals anyway 
    *h = dht11_val[0];
    *t = dht11_val[2];
    return 0;
  }
  else {
    // invalid data
    return 1;
  }
}

// ---------------------------------------------------
// LCD DEVICE FUNCTIONS
// ---------------------------------------------------
int open_lcd_device() {
    int fd = open("/dev/rpilcd", O_RDWR);
    if (fd < 0) {
        printf("Can't open rpilcd driver\n");
        return fd;
    }
}

void close_lcd_device(int fd) {
    if (fd < 0) {
        printf("Can't close device. It is already closed\n");
        return;
    }
    close(fd);
}


int print_to_lcd_device(int fd, char* line1, char* line2) {
    if (fd < 0) {
        printf("Can't print to lcd device. It is closed.\n");
        return fd;
    }
    if (write(fd, "\\c", 3) < 0) {
        printf("Can't clear lcd device.\n");
        return -1;
    }
    if (write(fd, line1, strlen(line1)) < 0) {
        printf("Can't write first line to lcd device.\n");
        return -1;
    }
    if (line2 != NULL) {
        if (write(fd, "\\n", 3) < 0) {
            printf("Can't move cursor to the next line.\n");
            return -1;
        }
        if (write(fd, line2, strlen(line2)) < 0) {
            printf("Can't write second line to lcd device.\n");
            return -1;
        }
    }
    return 0;
}

// ---------------------------------------------------
// MAIN FUNCTION
// ---------------------------------------------------

int main(int argc, char *argv[]) {
  int h; //humidity
  int t; //temperature in degrees Celsius
  int opt;

  // error out if wiringPi can't be used
  if (wiringPiSetup()==-1) {
    printf("Error interfacing with WiringPi\n");
    exit(1);
  }
  
  // throw away the first 3 measurements
  for (int i=0; i<3; i++) {
    dht11_read_val(&h, &t);
    delay(3000);
  }
  
  // Init de lcd_driver device
  int lcdfd = open_lcd_device();

  int retval=1;
  int tries=0;
  char timebuf[17]="\0";
  char valbuf[17]="\0";
  while(1) {
    // read the sensor until we get a pair of valid measurements
    // but bail out if we tried too many times
    while (retval != 0 && tries < MAX_TRIES) {
      retval = dht11_read_val(&h, &t);
      if (retval == 0) {
        // FIRST LINE ON LCD DEVICE
        time_t current_time = time(NULL);
        struct tm tm = *localtime(&current_time);
        snprintf(timebuf, 17, "Czas: %02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);
        //printf("%4d-%02d-%02d,", tm.tm_year+1900, tm.tm_mon+1, tm.tm_mday);
        //printf("%02d:%02d,", tm.tm_hour, tm.tm_min);
        printf("%s [%d]\n", timebuf, (int)strlen(timebuf));

        // SECOND LINE ON LCD DEVICE
        snprintf(valbuf, 17, "Wil: %d,Temp: %d", h, t);
        printf("%s [%d]\n", valbuf, (int)strlen(valbuf));

        print_to_lcd_device(lcdfd, timebuf, valbuf);
      } else {
          delay(3000);
      }
      tries += 1;
    }
    if (tries < MAX_TRIES) {
      tries = 0;
      retval = 1;
      delay(2000);
    }
    else {
      break;
    }
  }

  close_lcd_device(lcdfd);
}
