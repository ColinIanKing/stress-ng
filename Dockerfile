FROM alpine:3 as build

RUN echo "@testing http://nl.alpinelinux.org/alpine/edge/testing" >> /etc/apk/repositories && \
    echo "@community http://nl.alpinelinux.org/alpine/edge/community" >> /etc/apk/repositories && \
    apk add --update build-base libaio-dev libattr libbsd-dev libcap-dev libcap-dev libgcrypt-dev jpeg-dev judy-dev keyutils-dev lksctp-tools-dev libatomic zlib-dev mpfr-dev

ADD . stress-ng

RUN cd stress-ng && mkdir install-root && \
    make && make DESTDIR=install-root/ install

####### actual image ########

FROM alpine:3

RUN echo "@testing http://nl.alpinelinux.org/alpine/edge/testing" >> /etc/apk/repositories && \
    echo "@community http://nl.alpinelinux.org/alpine/edge/community" >> /etc/apk/repositories && \
    apk add --update libaio libattr libbsd libcap libcap libgcrypt jpeg judy keyutils lksctp-tools libatomic mpfr && \
    rm -rf /tmp/* /var/tmp/* /var/cache/apk/* /var/cache/distfiles/*

COPY --from=build stress-ng/install-root/ /

ENTRYPOINT ["/usr/bin/stress-ng"]
CMD ["--help"]
