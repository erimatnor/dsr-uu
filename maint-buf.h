#ifndef _MAINT_BUF_H
#define _MAINT_BUF_H

#ifndef NO_DECLS

void maint_buf_set_max_len(unsigned int max_len);
int maint_buf_add(struct dsr_pkt *dp);
int maint_buf_del_all(struct in_addr nxt_hop);
int maint_buf_del(struct in_addr nxt_hop, unsigned short id);
void maint_buf_set_timeout(void);
void maint_buf_timeout(unsigned long data);

inline int crit_addr_id_del(void *pos, void *data);
inline int crit_addr_del(void *pos, void *data);

int maint_buf_init(void);
void maint_buf_cleanup(void);

#endif /* NO_DECLS */

#endif /* _MAINT_BUF_H */
