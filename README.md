# README

The [wormhole](https://github.com/novemus/wormhole) is a tool for forwarding a remote TCP service to a local interface. The original purpose of this utility is to extend the capabilities of the [plexus](https://github.com/novemus/plexus) tool and provide the ability to access a TCP service located on a computer behind the NAT. Since establishing a connection with a TCP service located behind NAT is quite problematic, unlike UDP, to solve this problem, `wormhole` creates a UDP tunnel between remote and local hosts and maps a remote service to a local interface. Of course, for this purpose, it must be used together with the `plexus` to pre-punch UDP hole(s) in NAT(s) of host(s). UDP tunnel implements the original protocol named [tubus](https://github.com/novemus/tubus), which guarantees the delivery and integrity of the transmitted data. An additional feature of the `tubus` is the ability to make yourself opaque using a pre-shared key in order to hide protocol details and the structure of transmitted data. This can be useful to protect the protocol from possible attacks. Apart from the case with NAT, the utility can be used as opaque tunnel for network applications with increased security requirements. Note that the obscuration of the tunnel is not a full-fledged encryption. Applications should take care of the encryption of transmitted data.

## Build

You can download [prebuild packages](https://github.com/novemus/wormhole/releases) for Debian and Windows platforms.

Project depends on `boost` library. Clone repository and run the following commands:

```console
cd ~
git clone --recurse-submodules https://github.com/novemus/wormhole.git
cd ~/wormhole
cmake -B ./build [-DBOOST_ROOT=...]
cmake --build ./build --target wormhole [wormhole_shared] [wormhole_static] [wormhole_ut]
cmake --build ./build --target install
```

To build libraries, specify the *wormhole_static* and *wormhole_shared* targets.

## Using

Launch following command with your arguments on the host that exports some service:
```console
wormhole --purpose=export --service=<ip:port> --gateway=<ip:port> --faraway=<ip:port> [--obscure=<64-bit-number>]
```

Launch following command with your arguments on the host that imports alien service:
```console
wormhole --purpose=import --service=<ip:port> --gateway=<ip:port> --faraway=<ip:port> [--obscure=<64-bit-number>]
```

`--purpose` - how to use the application in relation to the specified service: "export|import"

`--service` - endpoint to map the service being imported or endpoint of the service being exported: "ip:port"

`--gateway` - endpoint of the transport tunnel on the local public interface: "ip:port"

`--faraway` - endpoint of the transport tunnel on the remote public interface: "ip:port"

`--obscure` - pre-shared key to obscure the transport tunnel: "64-bit-number"

## Library

The `wormhole` library API is described in the [wormhole.h](https://github.com/novemus/wormhole/blob/master/wormhole.h) header.

## Bugs and improvements

Feel free to [report](https://github.com/novemus/wormhole/issues) bugs and [suggest](https://github.com/novemus/wormhole/issues) improvements. 

## License

The `wormhole` is licensed under the Apache License 2.0, which means that you are free to get and use it for commercial and non-commercial purposes as long as you fulfill its conditions. See the LICENSE.txt file for more details.

## Copyright

Copyright © 2023 Novemus Band. All Rights Reserved.
