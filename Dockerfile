FROM alpine:3 as build

RUN echo "@testing http://nl.alpinelinux.org/alpine/edge/testing" >> /etc/apk/repositories && \
    apk add --update build-base libaio-dev libattr libbsd-dev libcap-dev libcap-dev libgcrypt-dev jpeg-dev judy-dev@testing keyutils-dev lksctp-tools-dev libatomic zlib-dev kmod-dev xxhash-dev

ADD . stress-ng

RUN cd stress-ng && mkdir install-root && \
    make && make DESTDIR=install-root/ install

####### actual image ########

FROM alpine:3

RUN echo "@testing http://nl.alpinelinux.org/alpine/edge/testing" >> /etc/apk/repositories && \
    apk add --update libaio libattr libbsd libcap libcap libgcrypt jpeg judy@testing keyutils lksctp-tools libatomic zlib kmod-dev xxhash-dev && \
    rm -rf /tmp/* /var/tmp/* /var/cache/apk/* /var/cache/distfiles/*

COPY --from=build stress-ng/install-root/ /

ENTRYPOINT ["/usr/bin/stress-ng"]
CMD ["--help"]
