pipeline {
    agent any
    stages {
        stage('Deploy') {
            when { tag 'deploy' }
            steps {
                sh 'docker compose pull'
                sh 'docker compose up -d'
            }
        }
    }
}
