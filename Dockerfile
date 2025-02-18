FROM debian:12 as build

RUN apt-get update && apt-get install -y build-essential libacl1-dev zlib1g-dev libbsd-dev libeigen3-dev libcrypt-dev libjpeg-dev libmpfr-dev libgmp-dev libkeyutils-dev libapparmor-dev apparmor libaio-dev libcap-dev libsctp-dev libjudy-dev libatomic1 libkmod-dev libxxhash-dev libglvnd-dev libgbm-dev sed

ADD . stress-ng

#RUN cd stress-ng && mkdir install-root && rm -rf config config.h && sed -i 's/TARGET_CLONES/DISABLE_TARGET_CLONES/' config.h && sed -i 's/HAVE__bf16/DISABLE_HAVE__bf16/' config.h && VERBOSE=1 make && make DESTDIR=install-root/ install
RUN cd stress-ng && mkdir install-root && rm -rf configs config.h && VERBOSE=1 make -f Makefile.config && VERBOSE=1 make && make DESTDIR=install-root/ install

####### actual image ########

FROM debian:12

RUN apt-get update && apt-get install -y libacl1-dev zlib1g-dev libbsd-dev libeigen3-dev libcrypt-dev libjpeg-dev libmpfr-dev libgmp-dev libkeyutils-dev libapparmor-dev apparmor libaio-dev libcap-dev libsctp-dev libjudy-dev libatomic1 libkmod-dev libxxhash-dev libglvnd-dev libgbm-dev 

COPY --from=build stress-ng/install-root/ /

ENTRYPOINT ["/usr/bin/stress-ng"]
CMD ["--help"]
