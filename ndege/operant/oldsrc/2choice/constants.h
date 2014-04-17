/*
 *  FILE: constants.h
 *  AUTH: mcc
 *  DESC: 
 *  DATE: Tue Feb 19 13:15:53 2002
 *  $Id$
 */

#ifndef _CONSTANTS_H_
#define _CONSTANTS_H_

int consts_int(char *name);
double consts_double(char *name);
char *consts_string(char *name);

/* looks for file "name.const" */
void consts_init(char *name);

#endif /* _CONSTANTS_H_ */
