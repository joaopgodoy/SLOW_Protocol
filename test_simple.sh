#!/bin/bash

echo "Teste simples com mensagem pequena..."

{
    echo "data"
    echo "teste pequeno"
    echo "exit"
} | timeout 30 ./slow_peripheral

echo "Teste concluído."
