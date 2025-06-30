#!/bin/bash

# Script para testar envio de mensagem grande
echo "Testando envio de mensagem de 119KB..."

# Cria mensagem de teste se não existir
if [ ! -f test_big_message.txt ]; then
    python3 -c "print('a' * 119000)" > test_big_message.txt
fi

echo "Tamanho do arquivo: $(ls -lh test_big_message.txt | awk '{print $5}')"

# Prepara input para o programa
{
    echo "data"
    cat test_big_message.txt
    echo ""  # Nova linha após a mensagem
    echo "exit"
} | timeout 120 ./slow_peripheral

echo "Teste concluído."
