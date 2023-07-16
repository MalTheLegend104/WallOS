// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// This file exists purely as a test ground. Any testing you need to do in the kernel that you 
// dont want to take up space and clutter kernel.cpp should go here. Anything goes, as long as
// it's for testing. 
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

void oogabooga() {
	__asm volatile("int $10");
}