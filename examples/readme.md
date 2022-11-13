# Get Started
Steps:

1 Compile SwarmKV first by executing `CMAKE .. && make` in parent directory

2 Run Hashicorp Consul `./consul agent -dev -config-dir=./swarmkv/test/consul.d/`

3 Create cluster with `../tools/swarmkv-cli --cluster-create simple-example 127.0.0.1:5210`

4 Run exmaple with `./simple_example`