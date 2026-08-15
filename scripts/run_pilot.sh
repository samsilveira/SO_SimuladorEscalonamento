#!/bin/bash

# Script para rodar os 4 arquivos de configuração e validar o piloto
make clean && make

mkdir -p results/raw
mkdir -p results/consolidated
mkdir -p scripts

echo "Compilando o simulador..."

CENARIOS=("equilibrado" "io_bound" "cpu_bound" "desbalanceado")

# Inicializa o arquivo consolidado de métricas
echo "scenario,total_cpu,total_io,ratio_io_cpu,ratio_cpu_io" > results/consolidated/metricas_piloto.csv  

for cenario in "${CENARIOS[@]}"; do
    echo "----------------------------------------"
    echo "[*] Executando cenário: $cenario"
    
    # Executar o simulador
    ./bin/simulador \
        --config configs/${cenario}.conf \
        --algorithm fcfs \
        --run-id pilot_${cenario} \
        --individual-output results/raw/pilot_${cenario}_indiv.csv \
        > results/raw/pilot_${cenario}_agg.csv

    # Analisar a saída estruturada CSV
    awk -v cen="$cenario" -F',' '
    NR>1 {
        sum_cpu += $9;   # Coluna total_cpu baseada no schema do README
        sum_io += $10;   # Coluna total_io baseada no schema do README
        if ($8 <= 2) alta++;       # Alta prioridade [0-2]
        else if ($8 >= 7) baixa++; # Baixa prioridade [7-9]
        total++;
    }
    END {
        print "-> Métricas Extraídas:"
        print "   Total CPU (ticks): " sum_cpu
        print "   Total I/O (ticks): " sum_io
        
        ratio_io_cpu = (sum_cpu > 0) ? sum_io / sum_cpu : 0;
        ratio_cpu_io = (sum_io > 0) ? sum_cpu / sum_io : 0;

        if (cen == "io_bound") {
            print "   Razão (I/O por CPU): " ratio_io_cpu
            if (ratio_io_cpu > 2.0) print "   [PASSOU] Razão E/S > CPU confirmada (> 2.0)"
            else print "   [FALHOU] Razão insuficiente."
        } 
        else if (cen == "cpu_bound") {
            print "   Razão (CPU por I/O): " ratio_cpu_io
            if (ratio_cpu_io > 2.0) print "   [PASSOU] Razão CPU > E/S confirmada (> 2.0)"
            else print "   [FALHOU] Razão insuficiente."
        }
        else if (cen == "desbalanceado") {
            pct_alta = (alta/total)*100;
            pct_baixa = (baixa/total)*100;
            print "   Alta Prioridade: " pct_alta "%"
            print "   Baixa Prioridade: " pct_baixa "%"
            if (pct_alta > 65 && pct_alta < 75) print "   [PASSOU] Proporção Alta ~70%"
            if (pct_baixa > 25 && pct_baixa < 35) print "   [PASSOU] Proporção Baixa ~30%"
        }
        
        printf "%s,%d,%d,%.2f,%.2f\n", cen, sum_cpu, sum_io, ratio_io_cpu, ratio_cpu_io >> "results/consolidated/metricas_piloto.csv"
    }' results/raw/pilot_${cenario}_indiv.csv
done

echo "----------------------------------------"
echo "Piloto finalizado com sucesso! Relatório salvo em results/consolidated/metricas_piloto.csv"