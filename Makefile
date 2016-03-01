# http://www.gnu.org/prep/standards/html_node/Standard-Targets.html#Standard-Targets

all: build

pkgconfig:
	@if [[ `which pkg-config` ]]; then echo "Success: Found pkg-config"; else "echo you need pkg-config installed" && exit 1; fi;

./mason_packages:
	./bootstrap.sh

./node_modules/nan:
	npm install `node -e "console.log(Object.keys(require('./package.json').dependencies).join(' '))"` \
	`node -e "console.log(Object.keys(require('./package.json').devDependencies).join(' '))"` --clang=1

./node_modules: ./node_modules/nan
	npm install node-pre-gyp

./build: pkgconfig ./node_modules ./mason_packages
	@export PKG_CONFIG_PATH="mason_packages/.link/lib/pkgconfig" && \
	  echo "*** Using osrm installed at `pkg-config libosrm --variable=prefix` ***" && \
	  ./node_modules/.bin/node-pre-gyp configure build --loglevel=error --clang=1

debug: pkgconfig ./node_modules ./mason_packages
	@export PKG_CONFIG_PATH="mason_packages/.link/lib/pkgconfig" && \
	  echo "*** Using osrm installed at `pkg-config libosrm --variable=prefix` ***" && \
	  export TARGET=Debug && ./bootstrap.sh && ./node_modules/.bin/node-pre-gyp configure build --debug --clang=1

coverage: pkgconfig ./node_modules ./mason_packages
	@export PKG_CONFIG_PATH="mason_packages/.link/lib/pkgconfig" && \
	  echo "*** Using osrm installed at `pkg-config libosrm --variable=prefix` ***" && \
	  ./node_modules/.bin/node-pre-gyp configure build --debug --clang=1 --coverage=true

verbose: pkgconfig ./node_modules ./mason_packages
	@export PKG_CONFIG_PATH="mason_packages/.link/lib/pkgconfig" && \
	  echo "*** Using osrm installed at `pkg-config libosrm --variable=prefix` ***" && \
	  ./node_modules/.bin/node-pre-gyp configure build --loglevel=verbose --clang=1

clean:
	(cd test/data/ && $(MAKE) clean)
	rm -rf ./build
	rm -rf ./lib/binding/*
	rm -rf ./node_modules/
	rm -f ./*tgz
	rm -rf ./mason_packages
	rm -rf ./deps

grind:
	valgrind --leak-check=full node node_modules/.bin/_mocha

# Note: this PATH setting is used to allow the localized tools to be used
# but your locally installed osrm-backend tool, if on PATH should override
shm: ./test/data/Makefile
	@PATH="${PATH}:./lib/binding" && echo "*** Using osrm-datastore from `which osrm-datastore` ***"
	@PATH="${PATH}:./lib/binding" && $(MAKE) -C ./test/data
	@PATH="${PATH}:./lib/binding" && osrm-datastore ./test/data/berlin-latest.osrm

test: shm
	npm test

.PHONY: test clean build shm
