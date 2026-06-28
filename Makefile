SHELL := /bin/bash

.PHONY: doctor fix setup build test bench release new-component new-driver

doctor:
	./scripts/doctor.sh

fix:
	./scripts/fix.sh

setup:
	./scripts/setup.sh

build:
	./scripts/build.sh

test:
	./scripts/test.sh

bench:
	./scripts/bench.sh

release:
	@echo "Usage: make release VERSION=v0.1.0"
	./scripts/release.sh $(VERSION)

new-component:
	@echo "Usage: make new-component NAME=my_component"
	./scripts/new_component.sh $(NAME)

new-driver:
	@echo "Usage: make new-driver NAME=my_driver"
	./scripts/new_driver.sh $(NAME)
