#include <iostream>
#include <thread>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <semaphore>
#include <atomic>
#include <chrono>
#include <random>
#include <array>
#include <algorithm>

// Definições globais
constexpr int TOTAL_JOGADORES = 4;
std::counting_semaphore<TOTAL_JOGADORES> semaforo_cadeiras(TOTAL_JOGADORES - 1); // Número de cadeiras disponíveis
std::condition_variable cond_var_musica;
std::mutex mutex_musica;
std::mutex mutex_estado;
std::atomic<bool> jogo_em_andamento{true};
bool rodada_finalizada = false;
bool musica_interrompida = false;
int linha_diagnostica = 0;
std::random_device rd;
std::mt19937 gerador(rd());
std::uniform_int_distribution<> distribuicao(1000, 4000);

std::array<int, TOTAL_JOGADORES> jogadores_ativos;

void exibir_linha_diagnostica(int linha) {
    std::cout << "Diagnóstico na linha " << linha << "\n";
}

// Classes
class JogoCadeiras {
private:
    int numero_jogadores;
    int numero_cadeiras; 
    int cadeiras_ocupadas = 0;  
    int jogador_eliminado = 0;
    int vencedor = 0;
    std::array<int, TOTAL_JOGADORES> estado_jogadores;    
    std::vector<int> jogadores_ativos;
    bool primeira_rodada = true;
public:
    JogoCadeiras(int num_jogadores) {
        this->numero_jogadores = num_jogadores;
        this->numero_cadeiras = num_jogadores - 1;
        this->cadeiras_ocupadas = 0;
        estado_jogadores.fill(0); // Inicializa corretamente o array
        for (int i = 0; i < this->numero_jogadores; i++) {
            jogadores_ativos.push_back(i + 1);
        }
    }

    void iniciar_rodada() {
        std::lock_guard<std::mutex> lock(mutex_estado);
        if (!primeira_rodada) {
            this->numero_jogadores--;     
            this->numero_cadeiras = this->numero_jogadores - 1; 
            this->cadeiras_ocupadas = 0;   
            this->jogador_eliminado = 0;
            std::cout << "\nRodada seguinte com " << numero_jogadores << " jogadores e " << numero_cadeiras << " cadeiras.\n Música está tocando... 🎶" << std::endl;
        } else {
            this->primeira_rodada = false;   
            std::cout << "\nIniciando jogo com " << numero_jogadores << " jogadores e " << numero_cadeiras << " cadeiras.\n Música está tocando... 🎶" << std::endl; 
        }
        for (int i = 0; i < this->estado_jogadores.size(); i++) {
            this->estado_jogadores[i] = 0;
        }
    }

    void parar_musica() {
        std::lock_guard<std::mutex> lock(mutex_musica);
        musica_interrompida = true;
        cond_var_musica.notify_all();
    }

    void eliminar_jogador(int jogador_id) {
        for (int i = 0; i < this->jogadores_ativos.size(); i++) {
            if (this->jogadores_ativos[i] == jogador_id) {
                this->jogadores_ativos[i] = 0;
                this->jogador_eliminado = jogador_id;
                rodada_finalizada = true;
                cond_var_musica.notify_all();
                break;
            }
        }
    }

    void ocupar_cadeira(int jogador_id) {
        if (this->cadeiras_ocupadas < this->numero_cadeiras) {
            if (this->estado_jogadores[jogador_id] == 0) {
                this->cadeiras_ocupadas++;
                this->estado_jogadores[cadeiras_ocupadas] = jogador_id;
                this->vencedor = jogador_id;
            }
        } else {
            this->eliminar_jogador(jogador_id);
        }
    }

    void exibir_estado() {
        for (int i = 1; i < estado_jogadores.size(); i++) {
            if (estado_jogadores[i] != 0) {
                std::cout << "[Cadeira " << i << "]: Ocupada por Jogador " << estado_jogadores[i] << "\n";
            }
        }
        std::cout << "\nJogador " << this->jogador_eliminado << " foi eliminado por não conseguir uma cadeira!\n----------------------------------------------\n";
    }

    bool esta_ativo(int jogador_id) {
        std::lock_guard<std::mutex> lock(mutex_estado);
        for (int i = 0; i < this->jogadores_ativos.size(); i++) {
            if (this->jogadores_ativos[i] == jogador_id) {
                return true;
            }
        }
        return false;
    }

    int get_qtd_cadeira() { return this->numero_cadeiras; }
    int get_vencedor() { return this->vencedor; }
};

class Jogador {
public:
    Jogador(int id, JogoCadeiras& jogo)
        : id(id), jogo(jogo), ativo(true) {}

    void jogar() {
        while (ativo && jogo_em_andamento) {
            {
                std::unique_lock<std::mutex> lock(mutex_musica);
                cond_var_musica.wait(lock, [] { return musica_interrompida && !rodada_finalizada; });
                if (semaforo_cadeiras.try_acquire()) {
                    this->jogo.ocupar_cadeira(id);
                } else {
                    this->jogo.eliminar_jogador(id);
                }
                if (!this->jogo.esta_ativo(id)) {
                    this->ativo = false;
                    break;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

private:
    int id;
    JogoCadeiras& jogo;
    bool ativo;
};

class Coordenador {
public:
    Coordenador(JogoCadeiras& jogo)
        : jogo(jogo) {}

    void iniciar_jogo() {
        std::cout << "-----------------------------------------------\nBem-vindo ao Jogo das Cadeiras Concorrente!\n-----------------------------------------------" << std::endl;
        while (jogo_em_andamento) {
            {
                this->jogo.iniciar_rodada();
                std::this_thread::sleep_for(std::chrono::milliseconds(distribuicao(gerador)));
                std::cout << "\n> Música parou! Jogadores tentando se sentar..." << std::endl;
                std::cout << "\n-----------------------------------------------\n";
                this->jogo.parar_musica();
                std::unique_lock<std::mutex> lock(mutex_musica);
                cond_var_musica.wait(lock, [] { return rodada_finalizada; });
                musica_interrompida = false;
                this->jogo.exibir_estado();
                this->liberar_threads_eliminadas();
                rodada_finalizada = false;
            }
            if (this->jogo.get_qtd_cadeira() == 1) {
                jogo_em_andamento = false;
                break;
            }
        }
    }

    void liberar_threads_eliminadas() {
        std::lock_guard<std::mutex> lock(mutex_estado);
        semaforo_cadeiras.release(this->jogo.get_qtd_cadeira() - 1); // Libera a quantidade de permissões de cadeiras disponíveis
    }

private:
    JogoCadeiras& jogo;
};

// Função principal
int main() {
    JogoCadeiras jogo(TOTAL_JOGADORES);
    Coordenador coordenador(jogo);
    std::vector<std::thread> threads_jogadores;

    std::vector<Jogador> jogadores_objs;
    for (int i = 1; i <= TOTAL_JOGADORES; ++i) {
        jogadores_objs.emplace_back(i, jogo);
    }

    for (int i = 0; i < TOTAL_JOGADORES; ++i) {
        threads_jogadores.emplace_back(&Jogador::jogar, &jogadores_objs[i]);
    }

    std::thread coordenador_thread(&Coordenador::iniciar_jogo, &coordenador);

    for (auto& t : threads_jogadores) {
        if (t.joinable()) {
            t.join();
        }
    }

    if (coordenador_thread.joinable()) {
        coordenador_thread.join();
    }

    std::cout << "\n🏆 Vencedor: Jogador " << jogo.get_vencedor() << "!\n-----------------------------------------------\n\nObrigado por jogar!" << std::endl;
    return 0;
}
