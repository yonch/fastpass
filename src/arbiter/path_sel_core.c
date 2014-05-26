
#include "path_sel_core.h"

#include <rte_ip.h>
#include <rte_timer.h>
#include "../graph-algo/fp_ring.h"
#include "../graph-algo/path_selection.h"
#include "control.h"


int exec_path_sel_core(void *void_cmd_p)
{
	struct path_sel_core_cmd *cmd = (struct path_sel_core_cmd *)void_cmd_p;
	struct admitted_traffic *admitted;

	while (1) {
		while (fp_ring_dequeue(cmd->q_admitted, (void **)&admitted) != 0)
			/* busy wait */;

		select_paths(admitted, NUM_RACKS);

		fp_ring_enqueue(cmd->q_path_selected, (void *)admitted);

		/* manage timers: timer documentation asks for this to run on all cores
		 * there shouldn't be any timers on this core */
		rte_timer_manage();
	}
	return 0;
}
