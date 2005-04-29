#ifndef _DSR_IO_H
#define _DSR_IO_H

int dsr_recv(struct dsr_pkt *dp);
void dsr_start_xmit(struct dsr_pkt *dp);

#endif				/* _DSR_IO_H */
