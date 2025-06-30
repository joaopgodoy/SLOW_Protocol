# SLOW_Protocol

Trabalho prático da disciplina de Redes de Computadores.

**Autores:**
- Enzo Tonon Morente - 14568476 
- João Pedro Alves Notari Godoy - 14582076
- Letícia Barbosa Neves - 14582076

## Descrição

Este projeto implementa um cliente UDP que utiliza o protocolo SLOW (Slow Light Over Wireless), uma versão customizada de protocolo de transporte confiável sobre UDP. O cliente é capaz de estabelecer conexões, enviar dados com fragmentação automática, controlar fluxo através de janela deslizante, e realizar reconexões através do mecanismo "revive".

## Compilação e Execução

### Requisitos

- Compilador C++ com suporte a C++17 ou superior
- Sistema operacional Linux/Unix
- Bibliotecas padrão C++ e POSIX

### Comandos de Compilação

```bash
# Compilar o programa
make

# Compilar e executar imediatamente
make run

# Limpar arquivos compilados
make clean
```

### Execução Manual

```bash
# Após compilar com 'make'
./slow_peripheral
```

## Funcionalidades Principais

### 1. Protocolo SLOW

O protocolo implementa as seguintes características:

- **Handshake de Conexão**: Estabelecimento de sessão confiável
- **Controle de Fluxo**: Janela deslizante para controle de congestionamento
- **Fragmentação**: Divisão automática de mensagens grandes
- **Retransmissão**: Detecção de tempo limite e reenvio automático
- **Revive**: Reconexão rápida sem handshake completo

### 2. Controle de Fluxo e Confiabilidade

- **Janela Deslizante**: Tamanho máximo depende da central
- **Tempo Limite**: 2 segundos para retransmissão
- **Máximo de Tentativas**: 3 retransmissões antes de descartar
- **Confirmação Cumulativa**: Confirmação de todos os pacotes até o número especificado

## Interface do Usuário

O programa oferece uma interface interativa com os seguintes comandos:

### Comandos Disponíveis

```
> Comando (data/disconnect/revive/exit):
```

#### `data`
- Envia uma mensagem para o servidor
- Solicita ao usuário que digite a mensagem
- Fragmenta automaticamente mensagens grandes
- Aguarda confirmação antes de continuar

#### `disconnect`
- Encerra a sessão atual
- Armazena estado para possível reconexão
- Envia pacote de desconexão ao servidor

#### `revive`
- Reconecta usando sessão armazenada (handshake zero-way)
- Envia mensagem junto com o pedido de reconexão
- Mais eficiente que reconexão completa

#### `exit`
- Encerra o programa

## Exemplos de Uso

### Teste Simples

```bash
# Executar teste com mensagem pequena
./test_simple.sh
```

### Uso Interativo

```bash
./slow_peripheral

# No prompt que aparece:
> data
Digite a mensagem: Olá, servidor!

> disconnect

> revive
Digite a mensagem: Reconectando...

> exit
```

## Detalhes Técnicos

### Servidor de Destino

- **Endereço**: `slow.gmelodie.com`
- **Porta**: `7033`

### Tratamento de Erros

- Detecção automática de pacotes perdidos
- Reenvio automático com backoff
- Descarte após máximo de tentativas

## Arquivos do Projeto

- `slow_peripheral.cpp`: Código principal do cliente
- `Makefile`: Script de compilação
- `test_simple.sh`: Teste com mensagem pequena

## Observações

- O programa requer conexão com a internet para acessar o servidor
- Tempos limite podem ocorrer em conexões instáveis
- Mensagens muito grandes são automaticamente fragmentadas
- O estado da sessão é preservado para reconexões rápidas via "revive"
