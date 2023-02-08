# README

The `wormhole` is a tool for forwarding a remote TCP service to a local interface. The original purpose of this utility is to extend the capabilities of the [plexus](https://github.com/novemus/plexus) tool and provide an ability to access a TCP service located on a computer behind the NAT. Since establishing a connection with a TCP service located behind NAT is quite problematic, unlike UDP, to solve this problem, `wormhole` creates a UDP tunnel between remote and local hosts and maps the remote service to the local interface. Of course, for this purpose, it must be used together with `plexus` to pre-punch UDP hole(s) in NAT(s) of host(s). UDP tunnel implements the original protocol named `tubus`, which guarantees the delivery and integrity of the transmitted data. An additional feature of the `tubus` is the ability to make yourself opaque using a pre-shared key in order to hide protocol details and the structure of transmitted data. This can be useful to protect the protocol from possible attacks. Apart from the case with NAT, the utility can be used as opaque tunnel for network applications with increased security requirements. Note that the obscuration of the tunnel is not a full-fledged encryption. Applications should take care of the encryption of transmitted data.

## Build

Project depends on `boost` library. Clone repository and run the following commands:

```console
cd ~
git clone git@github.com:novemus/wormhole.git
cd ~/wormhole
cmake -B ./build [-DBOOST_ROOT=...]
cmake --build ./build --target wormhole
cmake --build ./build --target install
```

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

## Bugs and improvements

Feel free to [report](https://github.com/novemus/wormhole/issues) bugs and [suggest](https://github.com/wormhole/plexus/issues) improvements. 

## License

The `wormhole` is licensed under the Apache License 2.0, which means that you are free to get and use it for commercial and non-commercial purposes as long as you fulfill its conditions. See the LICENSE.txt file for more details.

## Copyright

Copyright © 2023 Novemus Band. All Rights Reserved.
