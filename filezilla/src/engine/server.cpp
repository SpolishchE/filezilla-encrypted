#include <filezilla.h>
#include "server.h"

// Start of @td
#include "../interface/Options.h"
#include "crypto.h"
// End of @td

struct t_protocolInfo
{
	const enum ServerProtocol protocol;
	const wxString prefix;
	bool alwaysShowPrefix;
	unsigned int defaultPort;
	const bool translateable;
	const wxChar* const name;
	bool supportsPostlogin;
};

static const t_protocolInfo protocolInfos[] = {
	{ FTP,          _T("ftp"),    false, 21,  true,  wxTRANSLATE("FTP - File Transfer Protocol with optional encryption"),                 true  },
	{ SFTP,         _T("sftp"),   true,  22,  false, _T("SFTP - SSH File Transfer Protocol"),                              false },
	{ HTTP,         _T("http"),   true,  80,  false, _T("HTTP - Hypertext Transfer Protocol"),                             true  },
	{ HTTPS,        _T("https"),  true, 443,  true,  wxTRANSLATE("HTTPS - HTTP over TLS"),                                 true  },
	{ FTPS,         _T("ftps"),   true, 990,  true,  wxTRANSLATE("FTPS - FTP over implicit TLS/SSL"),                      true  },
	{ FTPES,        _T("ftpes"),  true,  21,  true,  wxTRANSLATE("FTPES - FTP over explicit TLS/SSL"),                     true  },
	{ INSECURE_FTP, _T("ftp"),    false, 21,  true,  wxTRANSLATE("FTP - Insecure File Transfer Protocol"), true  },
	{ UNKNOWN,      _T(""),       false, 21,  false, _T("") }
};

static const wxString typeNames[SERVERTYPE_MAX] = {
	wxTRANSLATE("Default (Autodetect)"),
	_T("Unix"),
	_T("VMS"),
	_T("DOS"),
	_T("MVS, OS/390, z/OS"),
	_T("VxWorks"),
	_T("z/VM"),
	_T("HP NonStop"),
	wxTRANSLATE("DOS-like with virtual paths"),
	_T("Cygwin")
};

static const t_protocolInfo& GetProtocolInfo(ServerProtocol protocol)
{
	unsigned int i = 0;
	for ( ; protocolInfos[i].protocol != UNKNOWN; ++i)
	{
		if (protocolInfos[i].protocol == protocol)
			break;
	}
	return protocolInfos[i];
}

CServer::CServer()
{
	Initialize();
}

bool CServer::ParseUrl(wxString host, const wxString& port, wxString user, wxString pass, wxString &error, CServerPath &path)
{
	unsigned long nPort = 0;
	if (!port.empty())
	{
		if (port.size() > 5 || !port.ToULong(&nPort) || !nPort || nPort > 65535)
		{
			error = _("Invalid port given. The port has to be a value from 1 to 65535.");
			error += _T("\n");
			error += _("You can leave the port field empty to use the default port.");
			return false;
		}
	}
	return ParseUrl(host, nPort, user, pass, error, path);
}

bool CServer::ParseUrl(wxString host, unsigned int port, wxString user, wxString pass, wxString &error, CServerPath &path)
{
	m_type = DEFAULT;

	if (host == _T(""))
	{
		error = _("No host given, please enter a host.");
		return false;
	}

	int pos = host.Find(_T("://"));
	if (pos != -1)
	{
		wxString protocol = host.Left(pos).Lower();
		host = host.Mid(pos + 3);
		if (protocol.Left(3) == _T("fz_"))
			protocol = protocol.Mid(3);
		m_protocol = GetProtocolFromPrefix(protocol.Lower());
		if (m_protocol == UNKNOWN)
		{
			// TODO: http:// once WebDAV is officially supported
			error = _("Invalid protocol specified. Valid protocols are:\nftp:// for normal FTP,\nsftp:// for SSH file transfer protocol,\nftps:// for FTP over SSL (implicit) and\nftpes:// for FTP over SSL (explicit).");
			return false;
		}
	}

	pos = host.Find('@');
	if (pos != -1)
	{
		// Check if it's something like
		//   user@name:password@host:port/path
		// => If there are multiple at signs, username/port ends at last at before
		// the first slash. (Since host and port never contain any at sign)

		int slash = host.Mid(pos + 1).Find('/');
		if (slash != -1)
			slash += pos + 1;

		int next_at = host.Mid(pos + 1).Find('@');
		while (next_at != -1)
		{
			next_at += pos + 1;
			if (slash != -1 && next_at > slash)
				break;

			pos = next_at;
			next_at = host.Mid(pos + 1).Find('@');
		}

		user = host.Left(pos);
		host = host.Mid(pos + 1);

		// Extract password (if any) from username
		pos = user.Find(':');
		if (pos != -1)
		{
			pass = user.Mid(pos + 1);
			user = user.Left(pos);
		}

		// Remove leading and trailing whitespace
		user.Trim(true);
		user.Trim(false);

		if (user == _T(""))
		{
			error = _("Invalid username given.");
			return false;
		}
	}
	else
	{
		// Remove leading and trailing whitespace
		user.Trim(true);
		user.Trim(false);

		if (user == _T("") && m_logonType != ASK && m_logonType != INTERACTIVE)
		{
			user = _T("anonymous");
			pass = _T("anonymous@example.com");
		}
	}

	pos = host.Find('/');
	if (pos != -1)
	{
		path = CServerPath(host.Mid(pos));
		host = host.Left(pos);
	}

	if (host[0] == '[')
	{
		// Probably IPv6 address
		pos = host.Find(']');
		if (pos == -1)
		{
			error = _("Host starts with '[' but no closing bracket found.");
			return false;
		}
		if (host[pos + 1])
		{
			if (host[pos + 1] != ':')
			{
				error = _("Invalid host, after closing bracket only colon and port may follow.");
				return false;
			}
			++pos;
		}
		else
			pos = -1;
	}
	else
		pos = host.Find(':');
	if (pos != -1)
	{
		if (!pos)
		{
			error = _("No host given, please enter a host.");
			return false;
		}

		long tmp;
		if (!host.Mid(pos + 1).ToLong(&tmp) || tmp < 1 || tmp > 65535)
		{
			error = _("Invalid port given. The port has to be a value from 1 to 65535.");
			return false;
		}
		port = tmp;
		host = host.Left(pos);
	}
	else
	{
		if (!port)
			port = GetDefaultPort(m_protocol);
		else if (port > 65535)
		{
			error = _("Invalid port given. The port has to be a value from 1 to 65535.");
			return false;
		}
	}

	host.Trim(true);
	host.Trim(false);

	if (host == _T(""))
	{
		error = _("No host given, please enter a host.");
		return false;
	}

	m_host = host;

	if (m_host[0] == '[')
	{
		m_host.RemoveLast();
		m_host = m_host.Mid(1);
	}

	m_port = port;
	m_user = user;
	// Start of @td
	SetPass(pass);
	// End of @td
	m_account = _T("");
	if (m_logonType != ASK && m_logonType != INTERACTIVE)
	{
		if (m_user == _T("")) {
			m_logonType = ANONYMOUS;
		} else if (m_user == _T("anonymous")) {
			wxString pwd = GetPass(); // @td
			if (pwd.IsEmpty() || pwd == _T("anonymous@example.com")) // @td
				m_logonType = ANONYMOUS;
			else
				m_logonType = NORMAL;
		} else {
			m_logonType = NORMAL;
		}
	}

	if (m_protocol == UNKNOWN)
		m_protocol = GetProtocolFromPort(port);

	return true;
}

ServerProtocol CServer::GetProtocol() const
{
	return m_protocol;
}

ServerType CServer::GetType() const
{
	return m_type;
}

wxString CServer::GetHost() const
{
	return m_host;
}

unsigned int CServer::GetPort() const
{
	return m_port;
}

wxString CServer::GetUser() const
{
	if (m_logonType == ANONYMOUS)
		return _T("anonymous");

	return m_user;
}

// Start of @td
wxString CServer::GetPass(bool decrypt/*=true*/) const
{
	if (m_logonType == ANONYMOUS)
		return _T("anon@localhost");

	if(COptions::Get()->GetOptionVal(OPTION_ENCRYPT_PASSWORDS) && decrypt) { // @td
		LOG("GetPass(). Launching Decryption.");
		wxString result = wxString(CCrypto::Decrypt(m_pass).c_str(), wxConvUTF8);
		LOG("GetPass(). Launching Decryption done, returning password=" << result.mb_str());// 
		return result;
	}
	return m_pass;
}
// End of @td

wxString CServer::GetAccount() const
{
	if (m_logonType != ACCOUNT)
		return _T("");

	return m_account;
}

CServer& CServer::operator=(const CServer &op)
{
	m_protocol = op.m_protocol;
	m_type = op.m_type;
	m_host = op.m_host;
	m_port = op.m_port;
	m_logonType = op.m_logonType;
	m_user = op.m_user;
	// Start of @td
	SetPass(op.m_pass);
	// End of @td
	m_account = op.m_account;
	m_timezoneOffset = op.m_timezoneOffset;
	m_pasvMode = op.m_pasvMode;
	m_maximumMultipleConnections = op.m_maximumMultipleConnections;
	m_encodingType = op.m_encodingType;
	m_customEncoding = op.m_customEncoding;
	m_postLoginCommands = op.m_postLoginCommands;
	m_bypassProxy = op.m_bypassProxy;
	m_name = op.m_name;

	return *this;
}

bool CServer::operator==(const CServer &op) const
{
	if (m_protocol != op.m_protocol)
		return false;
	else if (m_type != op.m_type)
		return false;
	else if (m_host != op.m_host)
		return false;
	else if (m_port != op.m_port)
		return false;
	else if (m_logonType != op.m_logonType)
		return false;
	else if (m_logonType != ANONYMOUS)
	{
		if (m_user != op.m_user)
			return false;

		if (m_logonType == NORMAL)
		{
			if (GetPass() != op.m_pass)
				return false;
		}
		else if (m_logonType == ACCOUNT)
		{
			if (GetPass() != op.m_pass)
				return false;
			if (m_account != op.m_account)
				return false;
		}
	}
	if (m_timezoneOffset != op.m_timezoneOffset)
		return false;
	else if (m_pasvMode != op.m_pasvMode)
		return false;
	else if (m_encodingType != op.m_encodingType)
		return false;
	else if (m_encodingType == ENCODING_CUSTOM)
	{
		if (m_customEncoding != op.m_customEncoding)
			return false;
	}
	if (m_postLoginCommands != op.m_postLoginCommands)
		return false;
	if (m_bypassProxy != op.m_bypassProxy)
		return false;

	// Do not compare number of allowed multiple connections

	return true;
}

bool CServer::operator<(const CServer &op) const
{
	if (m_protocol < op.m_protocol)
		return true;
	else if (m_protocol > op.m_protocol)
		return false;

	if (m_type < op.m_type)
		return true;
	else if (m_type > op.m_type)
		return false;

	int cmp = m_host.Cmp(op.m_host);
	if (cmp < 0)
		return true;
	else if (cmp > 0)
		return false;

	if (m_port < op.m_port)
		return true;
	else if (m_port > op.m_port)
		return false;

	if (m_logonType < op.m_logonType)
		return true;
	else if (m_logonType > op.m_logonType)
		return false;

	if (m_logonType != ANONYMOUS)
	{
		cmp = m_user.Cmp(op.m_user);
		if (cmp < 0)
			return true;
		else if (cmp > 0)
			return false;

		if (m_logonType == NORMAL)
		{
			cmp = GetPass().Cmp(op.m_pass);
			if (cmp < 0)
				return true;
			else if (cmp > 0)
				return false;
		}
		else if (m_logonType == ACCOUNT)
		{
			cmp = GetPass().Cmp(op.m_pass);
			if (cmp < 0)
				return true;
			else if (cmp > 0)
				return false;

			cmp = m_account.Cmp(op.m_account);
			if (cmp < 0)
				return true;
			else if (cmp > 0)
				return false;
		}
	}
	if (m_timezoneOffset < op.m_timezoneOffset)
		return true;
	else if (m_timezoneOffset > op.m_timezoneOffset)
		return false;

	if (m_pasvMode < op.m_pasvMode)
		return true;
	else if (m_pasvMode > op.m_pasvMode)
		return false;

	if (m_encodingType < op.m_encodingType)
		return true;
	else if (m_encodingType > op.m_encodingType)
		return false;

	if (m_encodingType == ENCODING_CUSTOM)
	{
		if (m_customEncoding < op.m_customEncoding)
			return true;
		else if (m_customEncoding > op.m_customEncoding)
			return false;
	}
	if (m_bypassProxy < op.m_bypassProxy)
		return true;
	else if (m_bypassProxy > op.m_bypassProxy)
		return false;

	// Do not compare number of allowed multiple connections

	return false;
}

bool CServer::operator!=(const CServer &op) const
{
	return !(*this == op);
}

bool CServer::EqualsNoPass(const CServer &op) const
{
	if (m_protocol != op.m_protocol)
		return false;
	else if (m_type != op.m_type)
		return false;
	else if (m_host != op.m_host)
		return false;
	else if (m_port != op.m_port)
		return false;
	else if ((m_logonType == ANONYMOUS) != (op.m_logonType == ANONYMOUS))
		return false;
	else if ((m_logonType == ACCOUNT) != (op.m_logonType == ACCOUNT))
		return false;
	else if (m_logonType != ANONYMOUS)
	{
		if (m_user != op.m_user)
			return false;
		if (m_logonType == ACCOUNT)
			if (m_account != op.m_account)
				return false;
	}
	if (m_timezoneOffset != op.m_timezoneOffset)
		return false;
	else if (m_pasvMode != op.m_pasvMode)
		return false;
	else if (m_encodingType != op.m_encodingType)
		return false;
	else if (m_encodingType == ENCODING_CUSTOM)
	{
		if (m_customEncoding != op.m_customEncoding)
			return false;
	}
	if (m_postLoginCommands != op.m_postLoginCommands)
		return false;
	if (m_bypassProxy != op.m_bypassProxy)
		return false;

	// Do not compare number of allowed multiple connections

	return true;
}

CServer::CServer(enum ServerProtocol protocol, enum ServerType type, wxString host, unsigned int port, wxString user, wxString pass /*=_T("")*/, wxString account /*=_T("")*/)
{
	Initialize();
	m_protocol = protocol;
	m_type = type;
	m_host = host;
	m_port = port;
	m_logonType = NORMAL;
	m_user = user;
	// Start of @td
	SetPass(pass);
	// End of @td
	m_account = account;
}

CServer::CServer(enum ServerProtocol protocol, enum ServerType type, wxString host, unsigned int port)
{
	Initialize();
	m_protocol = protocol;
	m_type = type;
	m_host = host;
	m_port = port;
}

void CServer::SetType(enum ServerType type)
{
	m_type = type;
}

enum LogonType CServer::GetLogonType() const
{
	return m_logonType;
}

void CServer::SetLogonType(enum LogonType logonType)
{
	wxASSERT(logonType != LOGONTYPE_MAX);
	m_logonType = logonType;
}

void CServer::SetProtocol(enum ServerProtocol serverProtocol)
{
	wxASSERT(serverProtocol != UNKNOWN);

	if (!GetProtocolInfo(serverProtocol).supportsPostlogin)
		m_postLoginCommands.clear();

	m_protocol = serverProtocol;
}

bool CServer::SetHost(wxString host, unsigned int port)
{
	if (host == _T(""))
		return false;

	if (port < 1 || port > 65535)
		return false;

	m_host = host;
	m_port = port;

	if (m_protocol == UNKNOWN)
		m_protocol = GetProtocolFromPort(m_port);

	return true;
}

bool CServer::SetUser(const wxString& user, const wxString& pass /*=_T("")*/, bool alreadyEncrypted/* = false*/) // @td
{
	LOG("SetUser() launched."); // @td
	if (m_logonType == ANONYMOUS)
		return true;

	if (user == _T(""))
	{
		if (m_logonType != ASK && m_logonType != INTERACTIVE)
			return false;
		LOG("SetUser() launched.");
		SetPass(_T(""), alreadyEncrypted);
	} else {
		SetPass(pass, alreadyEncrypted);
	}
	
	m_user = user;
	
	LOG("SetUser() finished"); // @td
	return true;
}
// start of @td
bool CServer::SetPass(const wxString& pass, bool alreadyEncrypted/* = false*/) {
	if(COptions::Get()->GetOptionVal(OPTION_ENCRYPT_PASSWORDS) && !alreadyEncrypted) { 
		LOG("SetPass(). Launching Encryption.");
		m_pass = wxString(CCrypto::Encrypt(pass).c_str(), wxConvUTF8);
		LOG("SetPass(). Encryption finished, m_pass=" << m_pass.mb_str());
	} else {
		m_pass = pass;
	}
	return true;
}
// end of @td

bool CServer::SetAccount(const wxString& account)
{
	if (m_logonType != ACCOUNT)
		return false;

	m_account = account;

	return true;
}

bool CServer::SetTimezoneOffset(int minutes)
{
	if (minutes > (60 * 24) || minutes < (-60 * 24))
		return false;

	m_timezoneOffset = minutes;

	return true;
}

int CServer::GetTimezoneOffset() const
{
	return m_timezoneOffset;
}

enum PasvMode CServer::GetPasvMode() const
{
	return m_pasvMode;
}

void CServer::SetPasvMode(enum PasvMode pasvMode)
{
	m_pasvMode = pasvMode;
}

void CServer::MaximumMultipleConnections(int maximumMultipleConnections)
{
	m_maximumMultipleConnections = maximumMultipleConnections;
}

int CServer::MaximumMultipleConnections() const
{
	return m_maximumMultipleConnections;
}

wxString CServer::FormatHost(bool always_omit_port /*=false*/) const
{
	wxString host = m_host;

	if (host.Find(':') != -1)
		host = _T("[") + host + _T("]");

	if (!always_omit_port)
	{
		if (m_port != GetDefaultPort(m_protocol))
			host += wxString::Format(_T(":%d"), m_port);
	}

	return host;
}

wxString CServer::FormatServer(const bool always_include_prefix /*=false*/) const
{
	wxString server = FormatHost();

	if (m_logonType != ANONYMOUS)
		server = GetUser() + _T("@") + server;

	const t_protocolInfo& info = GetProtocolInfo(m_protocol);
	if (!info.prefix.empty())
	{
		if (always_include_prefix || info.alwaysShowPrefix)
			server = info.prefix + _T("://") + server;
		else if (m_port != info.defaultPort)
			server = info.prefix + _T("://") + server;
	}

	return server;
}

void CServer::Initialize()
{
	m_protocol = UNKNOWN;
	m_type = DEFAULT;
	m_host = _T("");
	m_port = 21;
	m_logonType = ANONYMOUS;
	m_user = _T("");
	// Start of @td
	SetPass(_T(""));
	// End of @td
	m_account = _T("");
	m_timezoneOffset = 0;
	m_pasvMode = MODE_DEFAULT;
	m_maximumMultipleConnections = 0;
	m_encodingType = ENCODING_AUTO;
	m_customEncoding = _T("");
	m_bypassProxy = false;
}

bool CServer::SetEncodingType(enum CharsetEncoding type, const wxString& encoding /*=_T("")*/)
{
	if (type == ENCODING_CUSTOM && encoding == _T(""))
		return false;

	m_encodingType = type;
	m_customEncoding = encoding;

	return true;
}

bool CServer::SetCustomEncoding(const wxString& encoding)
{
	if (encoding == _T(""))
		return false;

	m_encodingType = ENCODING_CUSTOM;
	m_customEncoding = encoding;

	return true;
}

enum CharsetEncoding CServer::GetEncodingType() const
{
	return m_encodingType;
}

wxString CServer::GetCustomEncoding() const
{
	return m_customEncoding;
}

unsigned int CServer::GetDefaultPort(enum ServerProtocol protocol)
{
	const t_protocolInfo& info = GetProtocolInfo(protocol);

	return info.defaultPort;
}

enum ServerProtocol CServer::GetProtocolFromPort(unsigned int port, bool defaultOnly /*=false*/)
{
	for (unsigned int i = 0; protocolInfos[i].protocol != UNKNOWN; ++i)
	{
		if (protocolInfos[i].defaultPort == port)
			return protocolInfos[i].protocol;
	}

	if (defaultOnly)
		return UNKNOWN;

	// Else default to FTP
	return FTP;
}

wxString CServer::GetProtocolName(enum ServerProtocol protocol)
{
	const t_protocolInfo *protocolInfo = protocolInfos;
	while (protocolInfo->protocol != UNKNOWN)
	{
		if (protocolInfo->protocol != protocol)
		{
			++protocolInfo;
			continue;
		}

		if (protocolInfo->translateable)
			return wxGetTranslation(protocolInfo->name);
		else
			return protocolInfo->name;
	}

	return _T("");
}

enum ServerProtocol CServer::GetProtocolFromName(const wxString& name)
{
	const t_protocolInfo *protocolInfo = protocolInfos;
	while (protocolInfo->protocol != UNKNOWN)
	{
		if (protocolInfo->translateable)
		{
			if (wxGetTranslation(protocolInfo->name) == name)
				return protocolInfo->protocol;
		}
		else
		{
			if (protocolInfo->name == name)
				return protocolInfo->protocol;
		}
		++protocolInfo;
	}

	return UNKNOWN;
}

bool CServer::SetPostLoginCommands(const std::vector<wxString>& postLoginCommands)
{
	if (m_protocol != FTP && m_protocol != FTPS && m_protocol != FTPES)
	{
		// Currently, only regular FTP supports it
		return false;
	}

	m_postLoginCommands = postLoginCommands;
	return true;
}

enum ServerProtocol CServer::GetProtocolFromPrefix(const wxString& prefix)
{
	for (unsigned int i = 0; protocolInfos[i].protocol != UNKNOWN; ++i)
	{
		if (!protocolInfos[i].prefix.CmpNoCase(prefix))
			return protocolInfos[i].protocol;
	}

	return UNKNOWN;
}

wxString CServer::GetPrefixFromProtocol(const enum ServerProtocol protocol)
{
	const t_protocolInfo& info = GetProtocolInfo(protocol);

	return info.prefix;
}

void CServer::SetBypassProxy(bool val)
{
  m_bypassProxy = val;
}

bool CServer::GetBypassProxy() const
{
  return m_bypassProxy;
}

bool CServer::ProtocolHasDataTypeConcept(const enum ServerProtocol protocol)
{
	if (protocol == FTP || protocol == FTPS || protocol == FTPES)
		return true;

	return false;
}

wxString CServer::GetNameFromServerType(enum ServerType type)
{
	wxASSERT(type != SERVERTYPE_MAX);
	return wxGetTranslation(typeNames[type]);
}

enum ServerType CServer::GetServerTypeFromName(const wxString& name)
{
	for (int i = 0; i < SERVERTYPE_MAX; ++i)
	{
		enum ServerType type = (enum ServerType)i;
		if (name == CServer::GetNameFromServerType(type))
			return type;
	}

	return DEFAULT;
}

enum LogonType CServer::GetLogonTypeFromName(const wxString& name)
{
	if (name == _("Normal"))
		return NORMAL;
	else if (name == _("Ask for password"))
		return ASK;
	else if (name == _("Interactive"))
		return INTERACTIVE;
	else if (name == _("Account"))
		return ACCOUNT;
	else
		return ANONYMOUS;
}

wxString CServer::GetNameFromLogonType(enum LogonType type)
{
	wxASSERT(type != LOGONTYPE_MAX);

	switch (type)
	{
	case NORMAL:
		return _("Normal");
	case ASK:
		return _("Ask for password");
	case INTERACTIVE:
		return _("Interactive");
	case ACCOUNT:
		return _("Account");
	default:
		return _("Anonymous");
	}
}
