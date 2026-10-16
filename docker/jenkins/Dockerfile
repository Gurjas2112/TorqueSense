# The Jenkins controller for the GPU gates (spec Sections 28, 31; docs/adr/0004): the LTS image the spec
# names, with its plugins and its whole configuration baked in as code, so the controller is rebuilt,
# never clicked together. The job runs on the Windows agent `hsgpc-gpu`; the controller runs no builds.
FROM jenkins/jenkins:lts-jdk21
ENV JAVA_OPTS="-Xmx512m -Djenkins.install.runSetupWizard=false" \
    CASC_JENKINS_CONFIG=/usr/share/jenkins/ref/casc.yaml
COPY plugins.txt /usr/share/jenkins/ref/plugins.txt
RUN jenkins-plugin-cli --plugin-file /usr/share/jenkins/ref/plugins.txt
COPY casc.yaml /usr/share/jenkins/ref/casc.yaml
