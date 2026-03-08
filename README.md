# README

The [wormhole](https://github.com/novemus/wormhole) is the tool for forwarding TCP services via multiprotocol (UDP/TCP/SSL) safe tunnel. The original purpose of this utility is to extend the capabilities of the [plexus](https://github.com/novemus/plexus) tool and provide NAT/DPI-tolerance transport tunnel for private services. The UDP/TCP tunnels implemented with the [tubus](https://github.com/novemus/tubus) library and the SSL tunnel uses the OpenSSL. The `tubus` library provides the ability to obfuscate the transport channel using a pre-shared key. This can be useful to protect the tunnel from possible attacks. Note that the obscuration is not a full-fledged encryption. Applications should take care of the encryption of transmitted data.

## Build

You can download [prebuild packages](https://github.com/novemus/wormhole/releases) for Debian and Windows platforms.

Project depends on `boost` and `tubus` library. Clone repository and run the following commands:

```console
$ cd ~
$ git clone https://github.com/novemus/wormhole.git
$ cd ~/wormhole
$ [CMAKE_PREFIX_PATH=...] cmake -B ./build -DCMAKE_BUILD_TYPE=Release [-DBOOST_ROOT=...] [-DBUILD_SHARED_LIBS=ON] [-DWORMHOLE_SKIP_TEST_RULES=OFF]
$ cmake --build ./build --config Release --target all
$ cmake --build ./build --target install
```

## Using

Launch following command with your arguments on the host that exports some service:
```console
$ export WORMHOLE_SECRET=<64-bit-number>
$ wormhole --purpose=export --service=<ip:port> --gateway=<ip:port> --faraway=<ip:port>
```

Launch following command with your arguments on the host that imports alien service:
```console
$ export WORMHOLE_SECRET=<64-bit-number>
$ wormhole --purpose=import --service=<ip:port> --gateway=<ip:port> --faraway=<ip:port>
```

Main command line options:

`--purpose` - how to use the application in relation to the specified service: **export|import**

`--service` - endpoint to map the service being imported or endpoint of the service being exported: **ip:port**

`--gateway` - endpoint of the transport tunnel on the local public interface: **ip:port**

`--faraway` - endpoint of the transport tunnel on the remote public interface: **ip:port**

`--quality` - tunnel protocol and connection startegy: **udp:client|udp:server|tcp:client|tcp:server|ssl:client|ssl:server**

Environment:

`WORMHOLE_SECRET` - pre-shared key to obscure the UDP/TCP tunnel, 64-bit number

`WORMHOLE_CERT` - path to the host SSL certificate, PEM file

`WORMHOLE_KEY` - path to the host SSL private key, PEM file

`WORMHOLE_CA` - path to the CA certificate, PEM file

## Library

The `wormhole` library API is described in the [wormhole.h](https://github.com/novemus/wormhole/blob/master/wormhole.h) header.

## Bugs and improvements

Feel free to [report](https://github.com/novemus/wormhole/issues) bugs and [suggest](https://github.com/novemus/wormhole/issues) improvements. 

## License

The `wormhole` is licensed under the Apache License 2.0, which means that you are free to get and use it for commercial and non-commercial purposes as long as you fulfill its conditions. See the LICENSE.txt file for more details.

## Copyright

Copyright © 2023 Novemus Band. All Rights Reserved.
