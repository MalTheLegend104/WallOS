#include <scheduler/cpu.h>

void idle_task_main() {
	while (1) {
		__asm__ volatile("hlt");
	}
}

task_t* task_create(task_entry_t* entry_point, bool is_user) {

}

cpu_t* cpu_current(void) {

}