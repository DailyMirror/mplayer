#ifndef LIRC_H
#define LIRC_H

int 
mp_input_lirc_init(void);

int
mp_input_lirc_read(int fd,char* dest, int s);

void
mp_input_lirc_close(int fd);

#define /* LIRC_H */
