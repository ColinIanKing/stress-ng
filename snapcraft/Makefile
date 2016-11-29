VERSION=$(shell git tag | tail -1 | cut -c2-)
COMMITS=$(shell git log --oneline | wc -l)
SHA=$(shell git log -1 --oneline | cut -d' ' -f1)
DATE=$(shell date +'%Y%m%d')
V=$(VERSION)-$(DATE)-$(COMMITS)-$(SHA)

all: get_icon set_version
	snapcraft

set_version:
	cat snapcraft.yaml | sed 's/version: .*/version: $(V)/' > snapcraft-tmp.yaml
	mv snapcraft-tmp.yaml snapcraft.yaml

get_icon:
	@if [ ! -e setup/gui/icon.svg ]; \
	then \
		echo copying icon.svg; \
		mkdir -p setup/gui; \
		cp ../mascot/stress-ng.svg setup/gui/icon.svg; \
	fi;

clean:
	rm -rf setup *.snap
	snapcraft clean
	cat snapcraft.yaml | sed 's/version: .*/version: 0/' > snapcraft-tmp.yaml
	mv snapcraft-tmp.yaml snapcraft.yaml
