#ifndef __PS_SLURM_NODE_STATE
#define __PS_SLURM_NODE_STATE

#define NODE_STATE_NET        0x00000010 /* If a node is using Cray's
                                          * Network Performance
                                          * Counters but isn't in a
                                          * allocation. */
#define NODE_STATE_RES        0x00000020 /* If a node is in a
                                          * reservation (used primarily
                                          * to note a node isn't idle
                                          * for non-reservation jobs) */
#define NODE_STATE_UNDRAIN    0x00000040 /* Clear DRAIN flag for a node */
#define NODE_STATE_CLOUD      0x00000080 /* node comes from cloud */
#define NODE_RESUME           0x00000100 /* Restore a DRAINED, DRAINING, DOWN
                                          * or FAILING node to service (e.g.
                                          * IDLE or ALLOCATED). Used in
                                          * slurm_update_node() request */
#define NODE_STATE_DRAIN      0x00000200 /* do not allocated new work */
#define NODE_STATE_COMPLETING 0x00000400 /* node is completing allocated job */
#define NODE_STATE_NO_RESPOND 0x00000800 /* node is not responding */
#define NODE_STATE_POWER_SAVE 0x00001000 /* node is powered down by slurm */
#define NODE_STATE_FAIL       0x00002000 /* node is failing, do not allocate
                                          * new work */
#define NODE_STATE_POWER_UP   0x00004000 /* restore power or otherwise
                                          * configure a node */
#define NODE_STATE_MAINT      0x00008000 /* node in maintenance reservation */
#define NODE_STATE_REBOOT     0x00010000 /* node reboot requested */
#define NODE_STATE_CANCEL_REBOOT 0x00020000 /* cancel pending reboot */
#define NODE_STATE_POWERING_DOWN 0x00040000 /* node is powering down */

#endif /* __PS_SLURM_NODE_STATE */
