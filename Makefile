SHELL := /bin/bash
.PHONY: help vm provision sync build load unload test demo stop status clean

help:
	@printf '%s\n' \
	  'make vm        Create the cloud-init guest image and SSH key' \
	  'make provision Boot the guest and install driver build tools' \
	  'make build     Sync sources and build in the guest' \
	  'make load      Load the driver in the guest' \
	  'make test      Run the guest integration suite' \
	  'make demo      Run the short end-to-end demo' \
	  'make unload    Unload the driver in the guest' \
	  'make stop      Shut down the guest' \
	  'make status    Show VM process and SSH status'

vm:
	./scripts/vm_setup.sh

provision: vm
	./scripts/vm_start.sh
	./scripts/sync_to_guest.sh
	./scripts/guest_provision.sh

sync:
	./scripts/sync_to_guest.sh

build: sync
	./scripts/build_guest.sh

load:
	./scripts/load_driver.sh

unload:
	./scripts/unload_driver.sh

test:
	./tests/run_all.sh

demo: build
	./scripts/demo.sh

stop:
	./scripts/vm_stop.sh

status:
	./scripts/vm_status.sh

clean:
	./scripts/vm_ssh.sh 'cd ~/edupci && make -C driver clean && make -C user clean' || true
