#include <string>
#include <vector>
#include <map>
#include <functional>

namespace HonHengine {

    // On déclare GPURenderer pour que CmdContext le connaisse
    class GPURenderer;
    class SceneManager;
    // CmdContext : l'objet qui sera passé aux fonctions de commandes
    struct CmdContext {
		SceneManager* sceneManager;
        GPURenderer* renderer;
        std::vector<std::string> args;

        // Petit utilitaire pour vérifier le nombre d'arguments
        bool hasArgs(size_t n) const { return args.size() >= n; }
    };

    // Le type de fonction que tes commandes vont utiliser
    typedef std::function<void(CmdContext&)> CommandFunc;

    class CommandRegistry {
    public:
        // La méthode de ton build : on l'appelle souvent "Add" ou "Register"
        void Register(const std::string& name, CommandFunc func) {
            m_commands[name] = func;
        }

        // Pour exécuter une commande depuis le ShellThread
        void Execute(const std::string& name, GPURenderer* renderer, const std::vector<std::string>& args) {
            if (m_commands.find(name) != m_commands.end()) {
                CmdContext ctx = { renderer, args };
                m_commands[name](ctx);
            }
        }

    private:
        std::map<std::string, CommandFunc> m_commands;
    };
}