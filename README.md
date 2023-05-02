# datastore

Volume represents a hierarchy of nodes, each node contains key-value pairs.

Node data can be of the following types: uint32, uint64, float, double, string, blob.

Arbitrary volume nodes can be loaded into a vault.

Node view acts as an observer of volume nodes loaded into a vault.

Vaults and volumes can be accessed by multiple threads concurrently.

## How to build

```
cmake --preset <configurePreset-name>
cmake --build ./out/build/<configurePreset-name>
```