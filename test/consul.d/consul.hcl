datacenter = "dc1"
data_dir = "/tmp/consul"
encrypt = "XN0I7fHIY1+yLWm2PjegZ7U93nI/tmzNvtmBtZLuIfo="
verify_incoming = false
verify_outgoing = false
verify_server_hostname = false
acl {
  enabled = true
  default_policy = "allow"
  enable_token_persistence = true
}
performance {
  raft_multiplier = 1
}
