# Get Started
Steps:

1 Compile SwarmKV first by executing `CMAKE .. && make` in parent directory

2 Run Hashicorp Consul `./consul agent -dev -config-dir=./swarmkv/test/consul.d/`

3 Create cluster

 `../tools/swarmkv-cli --cluster-create simple-example 127.0.0.1:5210 127.0.0.1:5211`

4 Run exmaple with `./simple_example`