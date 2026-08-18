# Contribuição

## Fluxo de PR

Todo PR deve informar a issue relacionada, evidência de validação e revisor. O merge no `main` deve ocorrer somente após pelo menos uma aprovação de revisão cruzada.

## Validação mínima

Antes de abrir PR:

```sh
make clean && make
make test
```

O CI executa build release, build de desenvolvimento com sanitizers e testes. Se o build ou os testes falharem, o PR deve permanecer bloqueado.

## Resultados grandes

Não versione `results/raw/`. Versione somente resultados consolidados em `results/consolidated/` e figuras finais em `results/figures/`, junto dos comandos, seeds, configurações e commit usados para reproduzir a execução.
