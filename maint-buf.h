#ifndef _MAINT_BUF_H
#define _MAINT_BUF_H

int maint_buf_add(struct dsr_pkt *dp);
int maint_buf_del(struct in_addr nxt_hop);
void maint_buf_cleanup(void);

#endif /* _MAINT_BUF_H */
