#ifndef _MAINT_BUF_H
#define _MAINT_BUF_H

int maint_buf_add(struct dsr_pkt *dp);
int maint_buf_mark_acked(struct in_addr nxt_hop, unsigned short id);
int maint_buf_init(void);
void maint_buf_cleanup(void);

#endif /* _MAINT_BUF_H */
