# Painel de Controle de Acesso - Raspberry Pi Pico (BitDog Lab)

## 📌 Sumário  
- [📹 Demonstração](#-demonstração)  
- [🎯 Objetivo](#-objetivo)  
- [🛠️ Funcionalidades Obrigatórias](#️-funcionalidades-obrigatórias)  
- [📦 Componentes Utilizados](#-componentes-utilizados)  
- [⚙️ Compilação e Gravação](#️-compilação-e-gravação)  
- [📂 Estrutura do Código](#-estrutura-do-código)  
- [👨‍💻 Autor](#-autor)  

---

## 📹 Demonstração  
[clique aqui para acessar o vídeo](https://youtu.be/rTBprKNLdj0)

 
---

## 🎯 Objetivo  
Consolidar os conhecimentos adquiridos sobre sincronização de tarefas com **mutex** e **semáforos no FreeRTOS** através do desenvolvimento de um **painel de controle interativo**. Este sistema simula o controle de acesso de usuários a um determinado espaço (ex: laboratório, biblioteca), utilizando a placa BitDogLab com o RP2040 para fornecer feedback visual e sonoro.

---

## 🛠️ Funcionalidades Obrigatórias  
✅ **Contagem de Usuários:** Controla o número de usuários ativos simulados por botões.

✅ **Semáforo de Contagem:** Utiliza `xSemaphoreCreateCounting()` para gerenciar o número de vagas disponíveis.

✅ **Semáforos Binários:** Emprega `xSemaphoreCreateBinary()` para sinalizar eventos de entrada, saída e reset de forma eficiente a partir das ISRs.

✅ **Mutex para Display:** Garante o acesso exclusivo ao display OLED utilizando `xSemaphoreCreateMutex()` para evitar conflitos de escrita.

✅ **Interrupção de Reset:** Implementa interrupção para o botão do joystick (Botão J) que zera a contagem de usuários.

✅ **Feedback Visual (LED RGB):** O LED RGB indica o estado de ocupação do espaço:
    * **Azul:** Nenhum usuário logado (Vago).
    * **Verde:** Usuários ativos (contagem de 0 a `MAX_USUARIOS - 2`).
    * **Amarelo:** Apenas 1 vaga restante (`MAX_USUARIOS - 1`).
    * **Vermelho:** Capacidade máxima atingida (`MAX_USUARIOS`).
✅ **Sinalização Sonora (Buzzer):**
    * **Beep Curto:** Emitido ao tentar entrar no sistema quando a capacidade máxima é atingida.
    * **Beep Duplo:** Gerado ao resetar a contagem de usuários.

✅ **Exibição no Display OLED:** Mensagens claras e contagem de usuários são exibidas no display 128x64 via I2C, mostrando o status atual do sistema.

✅ **Debounce de Botões:** Implementação de um debounce por software (baseado em tempo na ISR) para garantir que cada pressionamento de botão seja registrado como um único evento.

✅ **Arquitetura Multitarefas com FreeRTOS:** O sistema é estruturado em **três tarefas principais** para gerenciar as operações de:
    * **`vTaskEntrada()`:** Aumenta o número de usuários ativos.
    * **`vTaskSaida()`:** Reduz o número de usuários ativos.
    * **`vTaskReset()`:** Zera a contagem de usuários.

✅ **Configuração Centralizada de IRQ:** Utiliza uma única função de *callback* de interrupção global (`gpio_irq_handler`) para processar eventos de múltiplos botões.

---

## 📦 Componentes Utilizados  
- **Placa de desenvolvimento:** BitDog Lab (RP2040) 
- **Botões:** Botões A (GPIO 5), B (GPIO 6) e Botão do Joystick (GPIO 22)
- **LED RGB:** Conectado aos pinos PWM (R: GPIO 13, G: GPIO 11, B: GPIO 12)
- **Buzzer passivo:** Conectado ao GPIO 21
- **Display OLED 128x64:** Via comunicação I2C (SSD1306) nos pinos SDA (GPIO 14) e SCL (GPIO 15)
- **Sistema operacional:** FreeRTOS 

---

## ⚙️ Compilação e Gravação  
Para compilar o projeto, siga os passos abaixo no terminal, a partir do diretório raiz do repositório:

```bash
git clone https://github.com/Ronaldo8617/PaineldeControle.git)
cd PaineldeControle
mkdir build
cd build
cmake ..
make
```
**Gravação:**  

Via VScode: Compile e execute diretamente na placa de desenvolvimento BitDog Lab, utilizando as ferramentas de depuração e upload.
Manual: Conecte o RP2040 no modo BOOTSEL (segurando o botão BOOTSEL na placa enquanto conecta o USB) e copie o arquivo .uf2 gerado na pasta build (PaineldeControle.uf2) para a unidade de disco que será montada.

## 📂 Estrutura do Código  

```plaintext
PaineldeControle/  
├── lib/  
│   ├── font.h               # Fonte de bitmap para o display OLED  
│   ├── ssd1306.c, h         # Driver de baixo nível para o display SSD1306 via I2C  
│   ├── display_init.c, h    # Inicialização do display e gerenciamento da instância global 'ssd'  
│   ├── buzzer.c, h          # Funções para controle do buzzer passivo via PWM  
│   ├── FreeRTOSConfig.h     # Arquivo de configuração do kernel FreeRTOS
├── CMakeLists.txt           # Configuração do projeto para o CMake
├── PaineldeControle.c       # Código principal contendo todas as tarefas, lógica de interrupções e hardware
├── README.md                # Este documento
```

## 👨‍💻 Autor  
**Nome:** Ronaldo César Santos Rocha  
**GitHub:** [Ronaldo8617]
