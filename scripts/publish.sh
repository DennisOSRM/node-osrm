
./node_modules/.bin/node-pre-gyp package ${NPM_FLAGS}

COMMIT_MESSAGE=$(git log --format=%B --no-merges -n 1 | tr -d '\n')
echo ${COMMIT_MESSAGE}

if [[ ${COMMIT_MESSAGE} =~ "[publish binary]" ]]; then
    echo "Publishing"
    ./node_modules/.bin/node-pre-gyp publish ${NPM_FLAGS}
elif [[ ${COMMIT_MESSAGE} =~ "[republish binary]" ]]; then
    echo "Re-Publishing"
    ./node_modules/.bin/node-pre-gyp unpublish publish ${NPM_FLAGS}
else
    echo "Skipping publishing"
fi;
