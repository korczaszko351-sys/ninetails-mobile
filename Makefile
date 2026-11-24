# Ninetails Mobile — główny Makefile
# Każdy cel deleguje do skryptu w build/. Nie budujemy nic "w miejscu" —
# wszystkie artefakty lądują w out/ (patrz build/env.sh).

SHELL := /bin/bash
.DEFAULT_GOAL := help

BUILD := build

.PHONY: help bootstrap host arcvm image flash ota clean distclean check

help: ## Lista dostępnych celów
	@echo "Ninetails Mobile — dostępne cele:"
	@grep -hE '^[a-zA-Z_-]+:.*?## .*$$' $(MAKEFILE_LIST) \
	  | awk 'BEGIN{FS=":.*?## "}{printf "  \033[1m%-14s\033[0m %s\n", $$1, $$2}'
	@echo ""
	@echo "Zmienne: BOARD=aerophone  VARIANT=userdebug|user|eng  JOBS=$$(nproc)"

bootstrap: ## Pobierz depot_tools + repo i zsynchronizuj źródła
	@$(BUILD)/bootstrap.sh

host: ## Zbuduj obraz hosta (Chromium OS, board aerophone)
	@$(BUILD)/build-host.sh

arcvm: ## Zbuduj obraz ARCVM (AOSP + AeroRadar + most RIL)
	@$(BUILD)/build-arcvm.sh

image: ## Złóż obraz urządzenia (partycje A/B, vbmeta, super)
	@$(BUILD)/make-image.sh

flash: ## Wgraj obraz na urządzenie (fastboot)
	@$(BUILD)/flash.sh

ota: ## Wygeneruj payload OTA (A/B, podpisany)
	@$(BUILD)/ota.sh

check: ## Sprawdź zależności hosta budującego
	@$(BUILD)/bootstrap.sh --check-only

clean: ## Usuń artefakty pośrednie (zostawia źródła)
	@rm -rf out/tmp out/arcvm/obj out/host/obj
	@echo "clean: ok"

distclean: ## Usuń całe out/ (uwaga: wymaga ponownego builda)
	@rm -rf out
	@echo "distclean: ok"
