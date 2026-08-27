# Serve the editor.
#
# It has to be served rather than opened as a file: ES modules will not load
# over file://, and Web MIDI and getUserMedia both need a secure context.
# localhost counts as one, so there is no certificate to deal with.

PORT ?= 8123
PYTHON ?= python

.PHONY: serve
serve:
	@echo "http://localhost:$(PORT)  (Chrome, Edge or Opera -- Web MIDI is not in Safari or Firefox)"
	@$(PYTHON) -m http.server $(PORT) --directory .
