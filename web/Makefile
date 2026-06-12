.PHONY: all clean

all: .build_done

node_modules: package.json package-lock.json
	npm install
	touch node_modules

.build_done: node_modules $(shell find src public -type f 2>/dev/null) index.html tsconfig.json tsconfig.app.json tsconfig.node.json vite.config.ts package.json
	npm run build
	touch .build_done

clean:
	rm -rf dist node_modules .build_done
