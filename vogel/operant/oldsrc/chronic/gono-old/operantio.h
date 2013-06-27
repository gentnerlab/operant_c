#ifndef OPERANTIO_H
#define OPERANTIO_H


void operant_write(int, int , int);
int operant_init();
void operant_clear(int box_id);
void operant_close();
int set_base_io_chans(int *indev, int *input, int *outdev, int *output, int boxid);
int operant_read(int box_id, int channel);

#endif
