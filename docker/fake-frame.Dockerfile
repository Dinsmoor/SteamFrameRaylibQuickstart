# "Fake Frame": rehearse scripts/frame.sh without the headset.
# Monado (simulated HMD) + sshd (key-only) + user `steamos` + docker/fake-steam.py,
# which provides the devkit pairing service (:32000, auto-approves) and answers
# the Steam devkit pipe protocol (create-shortcut, run-game).
#
#   make fake-frame                      # build + start; prints its address
#   scripts/frame.sh pair <that address> # same pairing flow as a real headset
FROM sfq-monado
RUN apt-get update && apt-get install -y --no-install-recommends openssh-server rsync python3 sudo \
    && rm -rf /var/lib/apt/lists/* \
    && useradd -m -s /bin/bash steamos && passwd -d steamos >/dev/null \
    && mkdir -p /run/sshd && sed -i 's/^#\?PasswordAuthentication.*/PasswordAuthentication no/' /etc/ssh/sshd_config
COPY fake-frame-entry.sh /usr/local/bin/fake-frame-entry.sh
COPY fake-steam.py /usr/local/bin/fake-steam.py
EXPOSE 22 32000
CMD ["/usr/local/bin/fake-frame-entry.sh"]
