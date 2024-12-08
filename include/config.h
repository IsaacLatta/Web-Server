#ifndef CONFIG_H
#define CONFIG_H

#include <tinyxml2.h>
#include <string>
#include <unordered_map>
#include <mutex>

#include "logger.h"

namespace cfg {

constexpr std::string viewer = "viewer";
constexpr std::string user = "user";
constexpr std::string admin = "admin";
constexpr std::string NO_HOST_NAME  = "";


using Endpoint = std::string;

struct Route {
    std::string method{""};
    Endpoint endpoint{""};
    std::string script{""};
    bool is_protected{false};
    std::string role{viewer};
    bool is_authenticator{false};
    Route() {}
};

using Routes = std::unordered_map<Endpoint, Route>;

class Config 
{
    public:
    static const Config* getInstance(const std::string& config_path = "");
    void initialize(const std::string& config_path);

    const Route* findRoute(const Endpoint& endpoint) const;
    const Routes* getRoutes() const {return &routes;}
    const std::string& getContentPath() const {return content_path;}
    const std::string getSecret() const {return secret;}
    const std::string getHostName() const {return host_name;}
    void printRoutes() const;

    private:
    Config();

    Config(Config&) = delete;
    void operator=(Config&) = delete;

    void loadRoutes(tinyxml2::XMLDocument* doc, const std::string& content_path);

    private:
    static Config INSTANCE;
    static std::once_flag initFlag;
    Routes routes;
    std::string secret = "top-secret";
    std::string content_path;
    std::string host_name;
};

std::string getRoleHash(const std::string& role);

};
#endif