namespace acl
{
    //addr_info
    acl::string gson(const addr_info &$obj);
    acl::json_node& gson(acl::json &$json, const addr_info &$obj);
    acl::json_node& gson(acl::json &$json, const addr_info *$obj);
    std::pair<bool,std::string> gson(acl::json_node &$node, addr_info &$obj);
    std::pair<bool,std::string> gson(acl::json_node &$node, addr_info *$obj);
    std::pair<bool,std::string> gson(const acl::string &str, addr_info &$obj);

    //cluster_config
    acl::string gson(const cluster_config &$obj);
    acl::json_node& gson(acl::json &$json, const cluster_config &$obj);
    acl::json_node& gson(acl::json &$json, const cluster_config *$obj);
    std::pair<bool,std::string> gson(acl::json_node &$node, cluster_config &$obj);
    std::pair<bool,std::string> gson(acl::json_node &$node, cluster_config *$obj);
    std::pair<bool,std::string> gson(const acl::string &str, cluster_config &$obj);

    //del_req
    acl::string gson(const del_req &$obj);
    acl::json_node& gson(acl::json &$json, const del_req &$obj);
    acl::json_node& gson(acl::json &$json, const del_req *$obj);
    std::pair<bool,std::string> gson(acl::json_node &$node, del_req &$obj);
    std::pair<bool,std::string> gson(acl::json_node &$node, del_req *$obj);
    std::pair<bool,std::string> gson(const acl::string &str, del_req &$obj);

    //del_resp
    acl::string gson(const del_resp &$obj);
    acl::json_node& gson(acl::json &$json, const del_resp &$obj);
    acl::json_node& gson(acl::json &$json, const del_resp *$obj);
    std::pair<bool,std::string> gson(acl::json_node &$node, del_resp &$obj);
    std::pair<bool,std::string> gson(acl::json_node &$node, del_resp *$obj);
    std::pair<bool,std::string> gson(const acl::string &str, del_resp &$obj);

    //exist_req
    acl::string gson(const exist_req &$obj);
    acl::json_node& gson(acl::json &$json, const exist_req &$obj);
    acl::json_node& gson(acl::json &$json, const exist_req *$obj);
    std::pair<bool,std::string> gson(acl::json_node &$node, exist_req &$obj);
    std::pair<bool,std::string> gson(acl::json_node &$node, exist_req *$obj);
    std::pair<bool,std::string> gson(const acl::string &str, exist_req &$obj);

    //exist_resp
    acl::string gson(const exist_resp &$obj);
    acl::json_node& gson(acl::json &$json, const exist_resp &$obj);
    acl::json_node& gson(acl::json &$json, const exist_resp *$obj);
    std::pair<bool,std::string> gson(acl::json_node &$node, exist_resp &$obj);
    std::pair<bool,std::string> gson(acl::json_node &$node, exist_resp *$obj);
    std::pair<bool,std::string> gson(const acl::string &str, exist_resp &$obj);

    //get_req
    acl::string gson(const get_req &$obj);
    acl::json_node& gson(acl::json &$json, const get_req &$obj);
    acl::json_node& gson(acl::json &$json, const get_req *$obj);
    std::pair<bool,std::string> gson(acl::json_node &$node, get_req &$obj);
    std::pair<bool,std::string> gson(acl::json_node &$node, get_req *$obj);
    std::pair<bool,std::string> gson(const acl::string &str, get_req &$obj);

    //get_resp
    acl::string gson(const get_resp &$obj);
    acl::json_node& gson(acl::json &$json, const get_resp &$obj);
    acl::json_node& gson(acl::json &$json, const get_resp *$obj);
    std::pair<bool,std::string> gson(acl::json_node &$node, get_resp &$obj);
    std::pair<bool,std::string> gson(acl::json_node &$node, get_resp *$obj);
    std::pair<bool,std::string> gson(const acl::string &str, get_resp &$obj);

    //raft_config
    acl::string gson(const raft_config &$obj);
    acl::json_node& gson(acl::json &$json, const raft_config &$obj);
    acl::json_node& gson(acl::json &$json, const raft_config *$obj);
    std::pair<bool,std::string> gson(acl::json_node &$node, raft_config &$obj);
    std::pair<bool,std::string> gson(acl::json_node &$node, raft_config *$obj);
    std::pair<bool,std::string> gson(const acl::string &str, raft_config &$obj);

    //set_req
    acl::string gson(const set_req &$obj);
    acl::json_node& gson(acl::json &$json, const set_req &$obj);
    acl::json_node& gson(acl::json &$json, const set_req *$obj);
    std::pair<bool,std::string> gson(acl::json_node &$node, set_req &$obj);
    std::pair<bool,std::string> gson(acl::json_node &$node, set_req *$obj);
    std::pair<bool,std::string> gson(const acl::string &str, set_req &$obj);

    //set_resp
    acl::string gson(const set_resp &$obj);
    acl::json_node& gson(acl::json &$json, const set_resp &$obj);
    acl::json_node& gson(acl::json &$json, const set_resp *$obj);
    std::pair<bool,std::string> gson(acl::json_node &$node, set_resp &$obj);
    std::pair<bool,std::string> gson(acl::json_node &$node, set_resp *$obj);
    std::pair<bool,std::string> gson(const acl::string &str, set_resp &$obj);

}///end of acl.
