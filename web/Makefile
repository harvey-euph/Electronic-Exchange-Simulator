.PHONY: all build clean

all: build

node_modules: package.json package-lock.json
	npm install

build: node_modules
	npm run build

clean:
	rm -rf dist node_modules
