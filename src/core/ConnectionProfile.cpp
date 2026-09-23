#include "ConnectionProfile.h"

#include "Crypto.h"

namespace
{
QString typeToString(ConnectionProfile::Type type)
{
    switch (type) {
    case ConnectionProfile::Type::Serial:
        return QStringLiteral("serial");
    case ConnectionProfile::Type::Telnet:
        return QStringLiteral("telnet");
    case ConnectionProfile::Type::Ssh:
        return QStringLiteral("ssh");
    }
    return QStringLiteral("serial");
}

ConnectionProfile::Type typeFromString(const QString &s)
{
    if (s == QStringLiteral("telnet"))
        return ConnectionProfile::Type::Telnet;
    if (s == QStringLiteral("ssh"))
        return ConnectionProfile::Type::Ssh;
    return ConnectionProfile::Type::Serial;
}

QString authMethodToString(SshConnectionSettings::AuthMethod m)
{
    return m == SshConnectionSettings::AuthMethod::PublicKey ? QStringLiteral("publickey") : QStringLiteral("password");
}

SshConnectionSettings::AuthMethod authMethodFromString(const QString &s)
{
    return s == QStringLiteral("publickey") ? SshConnectionSettings::AuthMethod::PublicKey : SshConnectionSettings::AuthMethod::Password;
}

// Secrets are never written in the clear: DPAPI-encrypt, then base64 so
// the result is plain JSON text.
QString protectToBase64(const QString &plaintext)
{
    if (plaintext.isEmpty())
        return {};
    return QString::fromLatin1(Crypto::protect(plaintext.toUtf8()).toBase64());
}

QString unprotectFromBase64(const QString &base64)
{
    if (base64.isEmpty())
        return {};
    return QString::fromUtf8(Crypto::unprotect(QByteArray::fromBase64(base64.toLatin1())));
}
} // namespace

QJsonObject ConnectionProfile::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("id")] = id.toString(QUuid::WithoutBraces);
    o[QStringLiteral("name")] = name;
    o[QStringLiteral("folder")] = folder;
    o[QStringLiteral("type")] = typeToString(type);
    o[QStringLiteral("autoReconnect")] = autoReconnect;
    o[QStringLiteral("keyboardProfile")] = keyboardProfile;

    switch (type) {
    case Type::Serial: {
        QJsonObject s;
        s[QStringLiteral("portName")] = serial.portName;
        s[QStringLiteral("baudRate")] = serial.baudRate;
        s[QStringLiteral("dataBits")] = static_cast<int>(serial.dataBits);
        s[QStringLiteral("parity")] = static_cast<int>(serial.parity);
        s[QStringLiteral("stopBits")] = static_cast<int>(serial.stopBits);
        s[QStringLiteral("flowControl")] = static_cast<int>(serial.flowControl);
        o[QStringLiteral("serial")] = s;
        break;
    }
    case Type::Telnet: {
        QJsonObject t;
        t[QStringLiteral("host")] = telnetHost;
        t[QStringLiteral("port")] = telnetPort;
        t[QStringLiteral("rawMode")] = rawMode;
        o[QStringLiteral("telnet")] = t;
        break;
    }
    case Type::Ssh: {
        QJsonObject s;
        s[QStringLiteral("host")] = ssh.host;
        s[QStringLiteral("port")] = ssh.port;
        s[QStringLiteral("username")] = ssh.username;
        s[QStringLiteral("authMethod")] = authMethodToString(ssh.authMethod);
        s[QStringLiteral("privateKeyPath")] = ssh.privateKeyPath;
        s[QStringLiteral("passwordProtected")] = protectToBase64(ssh.password);
        s[QStringLiteral("passphraseProtected")] = protectToBase64(ssh.passphrase);
        o[QStringLiteral("ssh")] = s;
        break;
    }
    }

    return o;
}

ConnectionProfile ConnectionProfile::fromJson(const QJsonObject &object)
{
    ConnectionProfile p;
    p.id = QUuid(object[QStringLiteral("id")].toString());
    if (p.id.isNull())
        p.id = QUuid::createUuid();
    p.name = object[QStringLiteral("name")].toString();
    p.folder = object[QStringLiteral("folder")].toString();
    p.type = typeFromString(object[QStringLiteral("type")].toString());
    p.autoReconnect = object[QStringLiteral("autoReconnect")].toBool(true);
    p.keyboardProfile = object[QStringLiteral("keyboardProfile")].toString();

    switch (p.type) {
    case Type::Serial: {
        const QJsonObject s = object[QStringLiteral("serial")].toObject();
        p.serial.portName = s[QStringLiteral("portName")].toString();
        p.serial.baudRate = s[QStringLiteral("baudRate")].toInt(QSerialPort::Baud115200);
        p.serial.dataBits = static_cast<QSerialPort::DataBits>(s[QStringLiteral("dataBits")].toInt(QSerialPort::Data8));
        p.serial.parity = static_cast<QSerialPort::Parity>(s[QStringLiteral("parity")].toInt(QSerialPort::NoParity));
        p.serial.stopBits = static_cast<QSerialPort::StopBits>(s[QStringLiteral("stopBits")].toInt(QSerialPort::OneStop));
        p.serial.flowControl = static_cast<QSerialPort::FlowControl>(s[QStringLiteral("flowControl")].toInt(QSerialPort::NoFlowControl));
        break;
    }
    case Type::Telnet: {
        const QJsonObject t = object[QStringLiteral("telnet")].toObject();
        p.telnetHost = t[QStringLiteral("host")].toString();
        p.telnetPort = static_cast<quint16>(t[QStringLiteral("port")].toInt(23));
        p.rawMode = t[QStringLiteral("rawMode")].toBool(false);
        break;
    }
    case Type::Ssh: {
        const QJsonObject s = object[QStringLiteral("ssh")].toObject();
        p.ssh.host = s[QStringLiteral("host")].toString();
        p.ssh.port = static_cast<quint16>(s[QStringLiteral("port")].toInt(22));
        p.ssh.username = s[QStringLiteral("username")].toString();
        p.ssh.authMethod = authMethodFromString(s[QStringLiteral("authMethod")].toString());
        p.ssh.privateKeyPath = s[QStringLiteral("privateKeyPath")].toString();
        p.ssh.password = unprotectFromBase64(s[QStringLiteral("passwordProtected")].toString());
        p.ssh.passphrase = unprotectFromBase64(s[QStringLiteral("passphraseProtected")].toString());
        break;
    }
    }

    return p;
}
