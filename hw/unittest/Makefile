all:
	$(MAKE) -C generic_queue
	$(MAKE) -C mem_streamer
	$(MAKE) -C cache_top
	$(MAKE) -C core_top
	$(MAKE) -C issue_top
	$(MAKE) -C local_mem_top
	$(MAKE) -C mem_unit_top
	$(MAKE) -C kmu
	$(MAKE) -C kmu_refactored_dcr_kd
	$(MAKE) -C kmu_refactored_dcr_host_buffer

run:
	$(MAKE) -C generic_queue run
	$(MAKE) -C mem_streamer run
	$(MAKE) -C cache_top run
	$(MAKE) -C core_top run
	$(MAKE) -C issue_top run
	$(MAKE) -C local_mem_top run
	$(MAKE) -C mem_unit_top run
	$(MAKE) -C kmu run
	$(MAKE) -C kmu_refactored_dcr_kd run
	$(MAKE) -C kmu_refactored_dcr_host_buffer run

clean:
	$(MAKE) -C generic_queue clean
	$(MAKE) -C mem_streamer clean
	$(MAKE) -C cache_top clean
	$(MAKE) -C core_top clean
	$(MAKE) -C issue_top clean
	$(MAKE) -C local_mem_top clean
	$(MAKE) -C mem_unit_top clean
	$(MAKE) -C kmu clean
	$(MAKE) -C kmu_refactored_dcr_kd clean
	$(MAKE) -C kmu_refactored_dcr_host_buffer clean