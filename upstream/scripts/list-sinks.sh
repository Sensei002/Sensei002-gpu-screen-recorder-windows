#!/bin/sh

pactl list | grep -E '(Description: Monitor of)|(Monitor Source: )'
