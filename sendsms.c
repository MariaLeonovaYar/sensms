#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <termios.h>
#include <errno.h>
#include <time.h>
#include <sys/ioctl.h>


#define BUFSIZE 4096
static unsigned char readbuf[BUFSIZE];
 
static struct termios term, gOriginalTTYAttrs;
 
void SendCmd(int fd, void *buf, size_t size) {
 
    write(0, buf, size);
    if(write(fd, buf, size) == -1) {
	fprintf(stderr, "SendCmd error. %d-%s\n", errno, strerror(errno));
	exit(1);
    }
}
 
void SendStrCmd(int fd, char *buf) {
    SendCmd(fd, buf, strlen(buf));
}
 
int ReadResp(int fd) {
    int len = 0;
    struct timeval timeout;
    int nfds = fd + 1;
    fd_set readfds;
    int select_ret;
 
    FD_ZERO(&readfds);
    FD_SET(fd, &readfds);
 
    // Wait a second
    timeout.tv_sec = 1;
    timeout.tv_usec = 500000;
 
    while (select_ret = select(nfds, &readfds, NULL, NULL, &timeout) > 0) {
	len += read(fd, readbuf + len, BUFSIZE - len);
	FD_ZERO(&readfds);
	FD_SET(fd, &readfds);
	timeout.tv_sec = 0;
	timeout.tv_usec = 500000;
    }
    readbuf[len] = 0;
    fprintf(stderr,"%s",readbuf);
    return len;
}
 
int InitConn(const char * dev, int speed) {
    int fd = open(dev, O_RDWR | O_EXCL | O_NOCTTY);
 
    if(fd == -1) {
	fprintf(stderr, "%i(%s)\n", errno, strerror(errno));
	exit(1);
    }
 
    ioctl(fd, TIOCEXCL);
    fcntl(fd, F_SETFL, 0);
 
    tcgetattr(fd, &term);
    memcpy((void *)&gOriginalTTYAttrs, (const void *)&term, sizeof(term));
 
    cfmakeraw(&term);
    cfsetspeed(&term, speed);
    term.c_cflag = CS8 | CLOCAL | CREAD;
    term.c_iflag = 0;
    term.c_oflag = 0;
    term.c_lflag = 0;
    term.c_cc[VMIN] = 0;
    term.c_cc[VTIME] = 0;
    tcsetattr(fd, TCSANOW, &term);
 
    return fd;
}

void CloseConn(int fd) {
    tcdrain(fd);
    tcsetattr(fd, TCSANOW, &gOriginalTTYAttrs);
    close(fd);
}
 
void SendAT(int fd) {
    char cmd[5];
 
    sprintf(cmd,"AT\r");
    SendCmd(fd, cmd, strlen(cmd));
}
 
void AT(int fd) {
    SendAT(fd);
    for (;;) {
	if(ReadResp(fd) != 0) {
	    if(strstr((const char *)readbuf,"OK") != NULL)
		break;
	}
	SendAT(fd);
    }
}

int SetPDUMode(int fd, int mode) {
    AT(fd);
    SendStrCmd(fd, "ATE0\r");
    ReadResp(fd);
    if(!strstr(readbuf,"OK"))
	return -1;
    if (mode) {
	SendStrCmd(fd, "AT+CSCS=\"UCS2\"\r");
	ReadResp(fd);
	if(!strstr(readbuf,"OK"))
	    return -1;
    } else {
	SendStrCmd(fd, "AT+CSCS=\"GSM\"\r");
	ReadResp(fd);
	if(!strstr(readbuf,"OK"))
	    return -1;
    }
    SendStrCmd(fd, "AT+CMGF=1\r");
    ReadResp(fd);
    if(!strstr(readbuf,"OK"))
	return -1;
    SendStrCmd(fd, "AT+CPMS=\"SM\",\"SM\",\"SM\"\r");
    ReadResp(fd);
    if(!strstr(readbuf,"OK"))
	return -1;
    return 0; 
}

int DeleteSMS(int fd, int num) {
    char at_cmd[64];

    SetPDUMode(fd, 0);
    snprintf(at_cmd, sizeof(at_cmd), "AT+CMGD=%d\r", num);
    SendStrCmd(fd, at_cmd);
    ReadResp(fd);
    if(strstr(readbuf,"OK"))
	return 0;
    return -1; 
}

int ListSMS(int fd, int mode) {

    SetPDUMode(fd, 0);
    if (mode)
	SendStrCmd(fd, "AT+CMGL=\"ALL\"\r");
    else
	SendStrCmd(fd, "AT+CMGL=\"REC UNREAD\"\r");
    ReadResp(fd);
    if(strstr(readbuf,"OK"))
	return 0;
    return -1; 
}
 
int SendAnyAT(int fd, char * text) {
    char at_cmd[1024];
    char * ptr;
    int len;
    int retry = 5;
    
    len = strlen(text);
    ptr = text;
    if (len > 2) {
	if ((text[0] == '\'') || (text[0] == '\"'))
	ptr = &text[1];
	if ((text[len-1] == '\'') || (text[len-1] == '\"'))
	    text[len-1] = 0;
    }
    snprintf(at_cmd, sizeof(at_cmd), "%s\r", ptr); //CTRL-Z
    SendStrCmd(fd, at_cmd);
    do {
	len = ReadResp(fd);
    } while(!strstr(readbuf,"OK") && retry-- > 0);
    if (retry <= 0)
	return -1;
    return 0;
}
 
int SendSMS(int fd, char *to, char *text, int mode) {
    char at_cmd[1024];
    char pdu[1024];
    char to_fixed[20];
    char * ptr, * ptr1 = pdu;
    int len;
    int pdu_len;
    int retry = 10;
    
    if (SetPDUMode(fd, mode) < 0)
	return -1;
    len = strlen(text);
    ptr = text;
    if (len > 2) {
	if ((text[0] == '\'') || (text[0] == '\"'))
	ptr = &text[1];
	if ((text[len-1] == '\'') || (text[len-1] == '\"'))
	    text[len-1] = 0;
    }
    while (*ptr) {
	if (*ptr == '\\') {
	    if (ptr[1] == 'r') {
		*ptr1 ++ = '\r';
		ptr ++;
		ptr ++;
		continue;
	    }
	    if (ptr[1] == 'n') {
		*ptr1 ++ = '\n';
		ptr ++;
		ptr ++;
		continue;
	    }
	}
	*ptr1 ++ = *ptr++;
    }
    *ptr1 = 0;
    if ((strlen(to) < 8) || (to[0] == '+'))
	snprintf(to_fixed, sizeof(to_fixed), "%s", to);
    else
	snprintf(to_fixed, sizeof(to_fixed), "+%s", to);
    snprintf(at_cmd,sizeof(at_cmd), "AT+CMGS=\"%s\"\n\r", to_fixed);
    SendStrCmd(fd, at_cmd);
    do {
	len = ReadResp(fd);
    } while(!strstr(readbuf,">") && retry-- > 0);
    if(retry > 0) {
	snprintf(at_cmd, sizeof(at_cmd), "%s\032", pdu); //CTRL-Z
	SendStrCmd(fd, at_cmd);
    }
//    if (mode && ptr)
//	ZFREE(ptr);
    if (retry <= 0)
	return -1;
    retry = 20;
    do {
	len = ReadResp(fd);
    } while(!strstr(readbuf,"+CMGS:") && retry-- > 0);
    if(retry > 0) {
	fprintf(stderr,"OK\n");
	return 0;//success
    } else {
	fprintf(stderr,"%s: %s\n",at_cmd,readbuf);
    }
    return -1;//failed
}

#define	SEND_AT		1
#define	SEND_SMS	2
#define	LIST_ALL_SMS	3
#define	LIST_UR_SMS	4
#define	DEL_SMS		5

int main(int argc, char **argv)
{
    int fd;
    int cmd = 0;
    int retcode = 0;
    int convflag = 0;

    if(argc < 3) {
	fprintf(stderr,"usage: %s PORT COMMAND [[DATA]]\n",argv[0]);
	fprintf(stderr,"examples:\t%s /dev/ttyUSB1 list\n",argv[0]);
	exit(1); 
    }
    if (strcmp(argv[2], "at")== 0) {
	cmd = SEND_AT;
	if(argc < 4) {
	    fprintf(stderr,"usage: %s PORT at 'AT COMMAND'\n",argv[0]);
	    fprintf(stderr,"examples:\t%s /dev/ttyUSB1 at AT+CFUN?\n",argv[0]);
	    exit(1); 
	}
    } else if (strcmp(argv[2], "send")== 0) {
	cmd = SEND_SMS;
	if(argc < 5) {
	    fprintf(stderr,"usage: %s PORT send PHONE 'TEXT'\n",argv[0]);
	    fprintf(stderr,"examples:\t%s /dev/ttyUSB1 +123456789012 'Hello!\nThis is sample SMS\n' [UCS2]\n",argv[0]);
	    exit(1); 
	}
    } else if (strcmp(argv[2], "all")== 0) {
	cmd = LIST_ALL_SMS;
    } else if (strcmp(argv[2], "unread")== 0) {
	cmd = LIST_UR_SMS;
    } else if (strcmp(argv[2], "del")== 0) {
	cmd = DEL_SMS;
	if(argc < 4) {
	    fprintf(stderr,"usage: %s PORT del NUMBER\n",argv[0]);
	    fprintf(stderr,"examples:\t%s /dev/ttyUSB1 del 5\n",argv[0]);
	    exit(1); 
	}
    }
    fd = InitConn(argv[1], 115200);
    switch (cmd) {
	case SEND_AT:
	    retcode = SendAnyAT(fd, argv[3]);
	break;
	case SEND_SMS:
	    retcode = SendSMS(fd, argv[3], argv[4], convflag);
	break;
	case DEL_SMS:
	    retcode = DeleteSMS(fd, atoi(argv[3]));
	break;
	case LIST_ALL_SMS:
	    retcode = ListSMS(fd, 1);
	break;
	case LIST_UR_SMS:
	    retcode = ListSMS(fd, 0);
	break;
	default:
	break;
    }
    CloseConn(fd);
    return retcode;
}

 